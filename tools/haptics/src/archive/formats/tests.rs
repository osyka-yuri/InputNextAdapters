#![allow(
    clippy::expect_used,
    reason = "Archive reader tests use fail-fast setup for small local fixtures."
)]

use super::*;
use crate::archive::test_support::{
    ba2_fixture, ba2_oversized_names_fixture, bsa_fixture, extract_one,
};
use std::fs::{self, File};
use tempfile::tempdir;
#[test]
fn reads_bsa_104_uncompressed_and_105_deflate_payloads() {
    let dir = tempdir().expect("tempdir");
    for (version, compressed) in [(104, true), (105, false)] {
        let path = dir.path().join(format!("test-{version}.bsa"));
        let payload = b"bounded haptics test audio";
        bsa_fixture(&path, version, payload, compressed);
        let entries = parse_archive(&path).expect("index BSA fixture");
        assert_eq!(entries[0].info.path, "Sound/Pulse.wav");
        if version == 105 {
            assert!(read_entry(&mut File::open(&path).expect("open archive"), &entries[0]).is_ok());
        } else {
            assert_eq!(
                read_entry(&mut File::open(&path).expect("open archive"), &entries[0])
                    .expect("zlib payload"),
                payload
            );
        }
    }
}

#[test]
fn explicitly_rejects_compressed_bsa_105_lz4_payloads() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("compressed-v105.bsa");
    bsa_fixture(
        &path,
        105,
        b"lz4 bytes are not silently treated as zlib",
        true,
    );
    let entry = parse_archive(&path).expect("index archive").remove(0);
    let error = read_entry(&mut File::open(path).expect("open archive"), &entry)
        .expect_err("unsupported codec");
    assert!(error.to_string().contains("LZ4"));
}

#[test]
fn reads_ba2_v1_and_v8_raw_and_deflate_payloads() {
    let dir = tempdir().expect("tempdir");
    for (version, compressed) in [(1, false), (8, true)] {
        let path = dir.path().join(format!("test-{version}.ba2"));
        let payload = b"bounded haptics test audio";
        ba2_fixture(&path, version, payload, compressed, "Sound/Audio/Pulse.wav");
        let (name, unpacked) = extract_one(&path).expect("parse BA2 fixture");
        assert_eq!(name, "Sound/Audio/Pulse.wav");
        assert_eq!(unpacked, payload);
    }
}

#[test]
fn rejects_archive_traversal_and_truncated_records() {
    let dir = tempdir().expect("tempdir");
    let traversal = dir.path().join("traversal.ba2");
    ba2_fixture(&traversal, 8, b"data", false, "../outside.wav");
    assert!(parse_archive(&traversal).is_err());
    let truncated = dir.path().join("truncated.ba2");
    fs::write(&truncated, b"BTDX\x08\0\0\0GNRL").expect("write truncated fixture");
    assert!(parse_archive(&truncated).is_err());
}

#[test]
fn rejects_aggregate_ba2_filename_metadata_before_reading_name_bytes() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("oversized-names.ba2");
    ba2_oversized_names_fixture(&path, 257, u16::MAX);
    let error = parse_archive(&path).expect_err("aggregate name budget");
    assert!(
        error
            .to_string()
            .contains("filename metadata exceeds 16 MiB")
    );
}
