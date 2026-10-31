use anyhow::{Context, Result, ensure};
use std::fs::File;
use std::path::Path;

use crate::MAX_ARCHIVE_ENTRIES;
use crate::archive::{ResourceInfo, display_virtual_path};

use super::{ArchiveEntry, PayloadCodec, read_at, u32_at, u64_at};

const MAX_BSA_NAME_BYTES: usize = 16 * 1024 * 1024;
#[derive(Clone, Copy)]
struct BsaFolder {
    count: u32,
}

struct BsaPending {
    folder: String,
    offset: u64,
    stored_size: usize,
    compressed: bool,
}

#[allow(
    clippy::too_many_lines,
    reason = "BSA table and folder blocks are validated together in their wire order."
)]
pub(super) fn parse(path: &Path, file: &mut File, file_len: u64) -> Result<Vec<ArchiveEntry>> {
    let version = u32_at(file, file_len, 4)?;
    ensure!(
        matches!(version, 104 | 105),
        "unsupported BSA version {version} in {}",
        path.display()
    );
    let folder_offset = u64::from(u32_at(file, file_len, 8)?);
    let archive_flags = u32_at(file, file_len, 12)?;
    let folder_count = u32_at(file, file_len, 16)? as usize;
    let file_count = u32_at(file, file_len, 20)? as usize;
    let folder_name_bytes = u32_at(file, file_len, 24)? as usize;
    let file_name_bytes = u32_at(file, file_len, 28)? as usize;
    ensure!(
        folder_count <= MAX_ARCHIVE_ENTRIES && file_count <= MAX_ARCHIVE_ENTRIES,
        "BSA count exceeds safety bound"
    );
    ensure!(
        folder_name_bytes <= MAX_BSA_NAME_BYTES && file_name_bytes <= MAX_BSA_NAME_BYTES,
        "BSA name tables exceed safety bound"
    );
    let folder_width = if version == 105 { 24_u64 } else { 16_u64 };
    let table_bytes = u64::try_from(folder_count)
        .context("BSA folder count does not fit an archive offset")?
        .checked_mul(folder_width)
        .context("BSA folder table size overflow")?;
    ensure!(
        folder_offset
            .checked_add(table_bytes)
            .is_some_and(|end| end <= file_len),
        "BSA folder table is out of bounds"
    );

    let mut folders = Vec::with_capacity(folder_count);
    for index in 0..folder_count {
        let at = folder_offset + index as u64 * folder_width;
        let count = u32_at(file, file_len, at + 8)?;
        // Bethesda's folder offset points into the file-name table for these archive flags;
        // folder blocks themselves follow the table in folder-index order.
        let _folder_name_offset = if version == 105 {
            u64_at(file, file_len, at + 16)?
        } else {
            u64::from(u32_at(file, file_len, at + 12)?)
        };
        folders.push(BsaFolder { count });
    }
    ensure!(
        folders
            .iter()
            .map(|folder| folder.count as usize)
            .sum::<usize>()
            == file_count,
        "BSA folder file counts do not match header"
    );

    let mut pending = Vec::with_capacity(file_count);
    let mut folder_cursor = folder_offset + table_bytes;
    let mut parsed_folder_name_bytes = 0_u64;
    for folder in folders {
        let name_len = read_at(file, file_len, folder_cursor, 1)?[0] as usize;
        let folder_name = String::from_utf8(read_at(file, file_len, folder_cursor + 1, name_len)?)
            .context("BSA folder name is not UTF-8")?;
        let folder_name = display_virtual_path(folder_name.trim_end_matches('\0'))?;
        let records_at = folder_cursor
            .checked_add(1 + name_len as u64)
            .context("BSA folder record offset overflow")?;
        let records_bytes = u64::from(folder.count)
            .checked_mul(16)
            .context("BSA file record size overflow")?;
        ensure!(
            records_at
                .checked_add(records_bytes)
                .is_some_and(|end| end <= file_len),
            "BSA file records are out of bounds"
        );
        for index in 0..folder.count as usize {
            let at = records_at + index as u64 * 16;
            let size_flags = u32_at(file, file_len, at + 8)?;
            let offset = u64::from(u32_at(file, file_len, at + 12)?);
            let stored_size = (size_flags & 0x3fff_ffff) as usize;
            let compressed = (archive_flags & 0x4 != 0) ^ (size_flags & 0x4000_0000 != 0);
            ensure!(offset < file_len, "BSA resource offset is out of bounds");
            pending.push(BsaPending {
                folder: folder_name.clone(),
                offset,
                stored_size,
                compressed,
            });
        }
        parsed_folder_name_bytes += name_len as u64;
        folder_cursor = records_at + records_bytes;
    }
    ensure!(
        parsed_folder_name_bytes == folder_name_bytes as u64,
        "BSA folder name byte count does not match header"
    );
    let names_at = folder_cursor;
    let raw_names = read_at(file, file_len, names_at, file_name_bytes)?;
    let mut names = raw_names
        .split(|byte| *byte == 0)
        .map(|bytes| String::from_utf8(bytes.to_vec()))
        .collect::<std::result::Result<Vec<_>, _>>()?;
    if names.last().is_some_and(String::is_empty) {
        names.pop();
    }
    ensure!(
        names.len() == file_count && pending.len() == file_count,
        "BSA filename table count mismatch"
    );

    let mut entries = Vec::with_capacity(file_count);
    for (item, name) in pending.into_iter().zip(names) {
        let name = display_virtual_path(&name)?;
        let resource_path = display_virtual_path(&format!("{}/{}", item.folder, name))?;
        ensure!(
            item.offset
                .checked_add(item.stored_size as u64)
                .is_some_and(|end| end <= file_len),
            "BSA payload is out of bounds: {resource_path}"
        );
        let (unpacked_size, codec) = if item.compressed {
            ensure!(
                item.stored_size >= 4,
                "BSA compressed resource is too short: {resource_path}"
            );
            let declared = u32_at(file, file_len, item.offset)? as usize;
            let codec = if version == 105 {
                PayloadCodec::Bsa105Lz4Unsupported
            } else {
                PayloadCodec::BsaZlib
            };
            (declared, codec)
        } else {
            (item.stored_size, PayloadCodec::Raw)
        };
        let info = ResourceInfo {
            path: resource_path,
            size_bytes: unpacked_size as u64,
            origin: path.to_string_lossy().into_owned(),
        };
        entries.push(ArchiveEntry {
            info,
            offset: item.offset,
            stored_size: item.stored_size,
            codec,
        });
    }
    Ok(entries)
}
