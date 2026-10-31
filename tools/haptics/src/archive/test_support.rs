#![allow(
    clippy::expect_used,
    clippy::cast_possible_truncation,
    reason = "Archive fixtures use fail-fast setup and fixed, small values so malformed builders fail at their source."
)]

use anyhow::{Context, Result};
use flate2::Compression;
use flate2::write::ZlibEncoder;
use std::fs::{self, File};
use std::io::{Seek, SeekFrom, Write};
use std::path::Path;

use crate::archive::formats::{parse_archive, read_entry};
pub(in crate::archive) fn bsa_fixture(path: &Path, version: u32, data: &[u8], compressed: bool) {
    let folder = b"Sound\0";
    let name = b"Pulse.wav\0";
    let width = if version == 105 { 24_usize } else { 16 };
    let table_end = 36 + width;
    let records_at = table_end + 1 + folder.len();
    let names_at = records_at + 16;
    let mut packed = Vec::new();
    if compressed {
        packed.extend_from_slice(&(data.len() as u32).to_le_bytes());
        let mut encoder = ZlibEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(data).expect("compress fixture");
        packed.extend(encoder.finish().expect("finish compression"));
    } else {
        packed.extend_from_slice(data);
    }
    let payload_at = names_at + name.len();
    let flags = 3 | if compressed { 4 } else { 0 };
    let mut bytes = Vec::new();
    bytes.extend_from_slice(b"BSA\0");
    push_u32(&mut bytes, version);
    push_u32(&mut bytes, 36);
    push_u32(&mut bytes, flags);
    push_u32(&mut bytes, 1);
    push_u32(&mut bytes, 1);
    push_u32(&mut bytes, folder.len() as u32);
    push_u32(&mut bytes, name.len() as u32);
    push_u32(&mut bytes, 0);
    push_u64(&mut bytes, 0);
    push_u32(&mut bytes, 1);
    if version == 105 {
        push_u32(&mut bytes, 0);
        push_u64(&mut bytes, names_at as u64);
    } else {
        push_u32(&mut bytes, names_at as u32);
    }
    bytes.push(folder.len() as u8);
    bytes.extend_from_slice(folder);
    push_u64(&mut bytes, 0);
    let size_flags = packed.len() as u32;
    push_u32(&mut bytes, size_flags);
    push_u32(&mut bytes, payload_at as u32);
    bytes.extend_from_slice(name);
    bytes.extend_from_slice(&packed);
    assert_eq!(bytes.len(), payload_at + packed.len());
    fs::write(path, bytes).expect("write BSA fixture");
}

pub(in crate::archive) fn ba2_fixture(
    path: &Path,
    version: u32,
    data: &[u8],
    compressed: bool,
    resource: &str,
) {
    let mut packed = Vec::new();
    if compressed {
        let mut encoder = ZlibEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(data).expect("compress fixture");
        packed = encoder.finish().expect("finish compression");
    } else {
        packed.extend_from_slice(data);
    }
    let payload_at = 60_usize;
    let names_at = payload_at + packed.len();
    let mut bytes = Vec::new();
    bytes.extend_from_slice(b"BTDX");
    push_u32(&mut bytes, version);
    bytes.extend_from_slice(b"GNRL");
    push_u32(&mut bytes, 1);
    push_u64(&mut bytes, names_at as u64);
    push_u32(&mut bytes, 0);
    bytes.extend_from_slice(b".wav");
    push_u32(&mut bytes, 0);
    push_u32(&mut bytes, 0);
    push_u64(&mut bytes, payload_at as u64);
    push_u32(&mut bytes, if compressed { packed.len() as u32 } else { 0 });
    push_u32(&mut bytes, data.len() as u32);
    push_u32(&mut bytes, 0);
    bytes.extend_from_slice(&packed);
    push_u16(&mut bytes, resource.len() as u16);
    bytes.extend_from_slice(resource.as_bytes());
    fs::write(path, bytes).expect("write BA2 fixture");
}

pub(in crate::archive) fn ba2_oversized_names_fixture(path: &Path, count: usize, name_len: u16) {
    let names_at = 24 + count * 36;
    let stride = 2 + usize::from(name_len);
    let file_len = names_at + count * stride;
    let mut file = File::create(path).expect("create oversized-name BA2 fixture");
    file.set_len(u64::try_from(file_len).expect("fixture length fits u64"))
        .expect("size sparse BA2 fixture");
    file.seek(SeekFrom::Start(0)).expect("seek header");
    file.write_all(b"BTDX").expect("BA2 magic");
    file.write_all(&8_u32.to_le_bytes()).expect("BA2 version");
    file.write_all(b"GNRL").expect("BA2 subtype");
    file.write_all(
        &u32::try_from(count)
            .expect("fixture count fits u32")
            .to_le_bytes(),
    )
    .expect("BA2 count");
    file.write_all(
        &u64::try_from(names_at)
            .expect("name offset fits u64")
            .to_le_bytes(),
    )
    .expect("BA2 name offset");
    file.seek(SeekFrom::Start(
        u64::try_from(names_at).expect("name offset fits u64"),
    ))
    .expect("seek BA2 names");
    for index in 0..count {
        let offset = names_at + index * stride;
        file.seek(SeekFrom::Start(
            u64::try_from(offset).expect("name offset fits u64"),
        ))
        .expect("seek name length");
        file.write_all(&name_len.to_le_bytes())
            .expect("write name length");
    }
}

fn push_u16(bytes: &mut Vec<u8>, value: u16) {
    bytes.extend_from_slice(&value.to_le_bytes());
}
fn push_u32(bytes: &mut Vec<u8>, value: u32) {
    bytes.extend_from_slice(&value.to_le_bytes());
}
fn push_u64(bytes: &mut Vec<u8>, value: u64) {
    bytes.extend_from_slice(&value.to_le_bytes());
}

pub(in crate::archive) fn extract_one(path: &Path) -> Result<(String, Vec<u8>)> {
    let entries = parse_archive(path)?;
    let entry = entries.first().context("fixture has entry")?;
    let mut file = File::open(path)?;
    Ok((entry.info().path.clone(), read_entry(&mut file, entry)?))
}
