use anyhow::{Context, Result, ensure};
use std::fs::File;
use std::path::Path;

use crate::MAX_ARCHIVE_ENTRIES;
use crate::archive::{ResourceInfo, display_virtual_path};

use super::{ArchiveEntry, PayloadCodec, read_at, u16_at, u32_at, u64_at};

const MAX_BA2_NAME_BYTES: usize = 16 * 1024 * 1024;
pub(super) fn parse(path: &Path, file: &mut File, file_len: u64) -> Result<Vec<ArchiveEntry>> {
    let version = u32_at(file, file_len, 4)?;
    ensure!(
        matches!(version, 1 | 8),
        "unsupported BA2 version {version} in {}",
        path.display()
    );
    ensure!(
        read_at(file, file_len, 8, 4)?.as_slice() == b"GNRL",
        "only general BA2 archives are supported: {}",
        path.display()
    );
    let count = u32_at(file, file_len, 12)? as usize;
    let names_offset = u64_at(file, file_len, 16)?;
    ensure!(
        count <= MAX_ARCHIVE_ENTRIES,
        "BA2 count exceeds safety bound"
    );
    let table_end = 24_u64
        .checked_add(count as u64 * 36)
        .context("BA2 record table size overflow")?;
    ensure!(
        table_end <= file_len && names_offset >= table_end && names_offset <= file_len,
        "BA2 record or filename table is out of bounds"
    );
    let mut pending = Vec::with_capacity(count);
    for index in 0..count {
        let at = 24 + index as u64 * 36;
        let offset = u64_at(file, file_len, at + 16)?;
        let packed = u32_at(file, file_len, at + 24)? as usize;
        let unpacked = u32_at(file, file_len, at + 28)? as usize;
        let stored_size = if packed == 0 { unpacked } else { packed };
        ensure!(
            offset
                .checked_add(stored_size as u64)
                .is_some_and(|end| end <= file_len),
            "BA2 payload is out of bounds"
        );
        pending.push((offset, stored_size, unpacked, packed != 0));
    }
    // Validate the entire variable-length name table before allocating any filename strings.
    // The per-entry u16 permits large names, so the count bound alone is not a memory bound.
    let mut cursor = names_offset;
    let mut total_name_bytes = 0_usize;
    for _ in 0..count {
        let length = u16_at(file, file_len, cursor)? as usize;
        cursor = cursor.checked_add(2).context("BA2 name offset overflow")?;
        total_name_bytes = total_name_bytes
            .checked_add(2 + length)
            .context("BA2 name table size overflow")?;
        ensure!(
            total_name_bytes <= MAX_BA2_NAME_BYTES,
            "BA2 filename metadata exceeds 16 MiB safety bound"
        );
        cursor = cursor
            .checked_add(u64::try_from(length)?)
            .context("BA2 name offset overflow")?;
        ensure!(cursor <= file_len, "BA2 filename table is out of bounds");
    }
    let mut names = Vec::with_capacity(count);
    cursor = names_offset;
    for _ in 0..count {
        let length = u16_at(file, file_len, cursor)? as usize;
        cursor = cursor.checked_add(2).context("BA2 name offset overflow")?;
        names.push(
            String::from_utf8(read_at(file, file_len, cursor, length)?)
                .context("BA2 filename is not UTF-8")?,
        );
        cursor = cursor
            .checked_add(u64::try_from(length)?)
            .context("BA2 name offset overflow")?;
    }
    let mut entries = Vec::with_capacity(count);
    for ((offset, stored_size, unpacked, compressed), name) in pending.into_iter().zip(names) {
        let resource_path = display_virtual_path(&name)?;
        let info = ResourceInfo {
            path: resource_path,
            size_bytes: unpacked as u64,
            origin: path.to_string_lossy().into_owned(),
        };
        let codec = if compressed {
            PayloadCodec::Zlib
        } else {
            PayloadCodec::Raw
        };
        entries.push(ArchiveEntry {
            info,
            offset,
            stored_size,
            codec,
        });
    }
    Ok(entries)
}
