use anyhow::{Context, Result, bail, ensure};
use flate2::read::ZlibDecoder;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use crate::MAX_RESOURCE_BYTES;
use crate::archive::ResourceInfo;

mod ba2;
mod bsa;
#[cfg(test)]
pub(in crate::archive) mod test_support;
#[cfg(test)]
mod tests;

const MAX_ARCHIVE_BYTES: u64 = 16 * 1024 * 1024 * 1024;
const MAX_PACKED_RESOURCE_BYTES: usize = MAX_RESOURCE_BYTES + 1024 * 1024;

#[derive(Clone, Copy, Debug)]
enum PayloadCodec {
    Raw,
    Zlib,
    BsaZlib,
    Bsa105Lz4Unsupported,
}

#[derive(Debug)]
pub(super) struct ArchiveEntry {
    info: ResourceInfo,
    offset: u64,
    stored_size: usize,
    codec: PayloadCodec,
}

impl ArchiveEntry {
    pub(super) fn info(&self) -> &ResourceInfo {
        &self.info
    }
}

pub(super) fn parse_archive(path: &Path) -> Result<Vec<ArchiveEntry>> {
    let mut file = File::open(path).with_context(|| format!("open archive {}", path.display()))?;
    let length = file.metadata()?.len();
    ensure!(
        length <= MAX_ARCHIVE_BYTES,
        "archive exceeds 16 GiB safety bound: {}",
        path.display()
    );
    ensure!(
        length >= 8,
        "archive header is truncated: {}",
        path.display()
    );
    match read_at(&mut file, length, 0, 4)?.as_slice() {
        b"BSA\0" => bsa::parse(path, &mut file, length),
        b"BTDX" => ba2::parse(path, &mut file, length),
        _ => bail!("unsupported archive magic in {}", path.display()),
    }
}
pub(super) fn read_entry(file: &mut File, entry: &ArchiveEntry) -> Result<Vec<u8>> {
    ensure!(
        entry.info.size_bytes <= MAX_RESOURCE_BYTES as u64,
        "resource exceeds 32 MiB unpacked limit: {}",
        entry.info.path
    );
    ensure!(
        entry.stored_size <= MAX_PACKED_RESOURCE_BYTES,
        "resource exceeds packed read limit: {}",
        entry.info.path
    );
    let file_len = file.metadata()?.len();
    let stored = read_at(file, file_len, entry.offset, entry.stored_size)?;
    match entry.codec {
        PayloadCodec::Raw => Ok(stored),
        PayloadCodec::Zlib => inflate_zlib(
            &stored,
            usize::try_from(entry.info.size_bytes).context("resource size does not fit memory")?,
            &entry.info.path,
        ),
        PayloadCodec::BsaZlib => {
            ensure!(
                stored.len() >= 4,
                "truncated BSA compressed resource: {}",
                entry.info.path
            );
            let expected =
                u32::from_le_bytes(stored[..4].try_into().context("BSA size prefix")?) as usize;
            ensure!(
                u64::try_from(expected)? == entry.info.size_bytes,
                "BSA size prefix mismatch: {}",
                entry.info.path
            );
            inflate_zlib(&stored[4..], expected, &entry.info.path)
        }
        PayloadCodec::Bsa105Lz4Unsupported => anyhow::bail!(
            "compressed BSA v105 resource uses LZ4, which this tool explicitly does not support: {}",
            entry.info.path
        ),
    }
}

fn inflate_zlib(packed: &[u8], expected: usize, path: &str) -> Result<Vec<u8>> {
    ensure!(
        expected <= MAX_RESOURCE_BYTES,
        "uncompressed resource exceeds limit: {path}"
    );
    let mut decoder = ZlibDecoder::new(packed);
    let mut output = Vec::with_capacity(expected);
    decoder
        .by_ref()
        .take(expected as u64 + 1)
        .read_to_end(&mut output)
        .context("inflate archive resource")?;
    ensure!(
        output.len() == expected,
        "decompressed size mismatch for {path}: expected {expected}, got {}",
        output.len()
    );
    Ok(output)
}

fn read_at(file: &mut File, file_len: u64, offset: u64, length: usize) -> Result<Vec<u8>> {
    ensure!(
        offset
            .checked_add(length as u64)
            .is_some_and(|end| end <= file_len),
        "archive read is out of bounds"
    );
    let mut bytes = vec![0; length];
    file.seek(SeekFrom::Start(offset))?;
    file.read_exact(&mut bytes)?;
    Ok(bytes)
}

fn u16_at(file: &mut File, file_len: u64, offset: u64) -> Result<u16> {
    let bytes = read_at(file, file_len, offset, 2)?;
    Ok(u16::from_le_bytes([bytes[0], bytes[1]]))
}
fn u32_at(file: &mut File, file_len: u64, offset: u64) -> Result<u32> {
    let bytes = read_at(file, file_len, offset, 4)?;
    Ok(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}
fn u64_at(file: &mut File, file_len: u64, offset: u64) -> Result<u64> {
    let bytes = read_at(file, file_len, offset, 8)?;
    Ok(u64::from_le_bytes([
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
    ]))
}
