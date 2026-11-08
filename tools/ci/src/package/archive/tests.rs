use std::collections::BTreeSet;
use std::fs;

use anyhow::Result;

use super::{
    ManifestFile, PackageExpectation, PackageKind, PackageManifest, enumerate_files,
    inspect_extracted_package, package_filename, required_adapter_payload, source_forbidden,
    validate_payload_shape, validate_plugin_exports, write_package_archive,
};
use crate::model::{Architecture, Product, SourceIdentity};
use crate::process::{CommandOutput, CommandSpec, Runner};

struct NoCommands;

impl Runner for NoCommands {
    fn run(&mut self, _spec: &CommandSpec) -> Result<CommandOutput> {
        anyhow::bail!("no Git command is expected for data-only source roots")
    }
}

#[test]
fn filenames_match_frozen_distribution_contract() {
    let expectation = PackageExpectation {
        product: Product::SkyrimSe,
        architecture: Some(Architecture::X64),
        version: "1.2.3".to_owned(),
        kind: PackageKind::Binary,
        source: SourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: Some("0".repeat(64)),
        },
    };
    assert_eq!(
        package_filename(&expectation),
        "InputNextAdapters-skyrim-se-1.2.3-windows-x64.zip"
    );
}

#[test]
fn all_plugins_require_the_exact_named_export_set() {
    for (product, names) in [
        (
            Product::SkyrimSe,
            &["SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"][..],
        ),
        (
            Product::SkyrimLe,
            &["SKSEPlugin_Load", "SKSEPlugin_Query"][..],
        ),
        (
            Product::Fallout4,
            &["F4SEPlugin_Load", "F4SEPlugin_Version"][..],
        ),
    ] {
        let mut exports = names
            .iter()
            .map(|name| (*name).to_owned())
            .collect::<Vec<_>>();
        assert!(validate_plugin_exports(product, &exports).is_ok());
        exports.push("UnexpectedPluginExport".to_owned());
        exports.sort();
        assert!(validate_plugin_exports(product, &exports).is_err());
    }
}

// Expected minimum payloads are intentionally defined separately
// from the production requirements to detect accidental changes.
const EXPECTED_FALLOUT4_PAYLOAD: &[&str] = &[
    "Data/F4SE/Plugins/InputNextFallout4.dll",
    "Data/F4SE/Plugins/InputNextFallout4.ini",
    "Data/F4SE/Plugins/InputNextFallout4.reference.ini",
    "README.md",
    "licenses/CommonLibF4-EXCEPTIONS.txt",
    "licenses/CommonLibF4-GPL-3.0.txt",
    "licenses/CommonLibShared-EXCEPTIONS.txt",
    "licenses/CommonLibShared-GPL-3.0.txt",
    "licenses/InputNext-MIT.txt",
    "licenses/THIRD_PARTY_NOTICES",
    "licenses/spdlog-MIT.txt",
];

const EXPECTED_SKYRIM_SE_PAYLOAD: &[&str] = &[
    "Data/SKSE/Plugins/InputNextSkyrim.dll",
    "Data/SKSE/Plugins/InputNextSkyrim.ini",
    "Data/SKSE/Plugins/InputNextSkyrim.reference.ini",
    "README.md",
    "licenses/CommonLibSSE-COPYING.txt",
    "licenses/CommonLibSSE-EXCEPTIONS.md",
    "licenses/CommonLibSSE-MIT.txt",
    "licenses/DirectXTK-LICENSE",
    "licenses/InputNext-MIT.txt",
    "licenses/THIRD_PARTY_NOTICES",
    "licenses/fmt-LICENSE",
    "licenses/rapidcsv-LICENSE",
    "licenses/spdlog-LICENSE",
];

const EXPECTED_SKYRIM_LE_PAYLOAD: &[&str] = &[
    "Data/SKSE/Plugins/InputNextSkyrimLE.dll",
    "Data/SKSE/Plugins/InputNextSkyrimLE.ini",
    "Data/SKSE/Plugins/InputNextSkyrimLE.reference.ini",
    "README.md",
    "licenses/InputNext-MIT.txt",
    "licenses/SKSE-source-terms.txt",
    "licenses/THIRD_PARTY_NOTICES",
];

fn test_expectation(product: Product, architecture: Architecture) -> PackageExpectation {
    PackageExpectation {
        product,
        architecture: Some(architecture),
        version: "0.1.0".to_owned(),
        kind: PackageKind::Binary,
        source: SourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: Some("0".repeat(64)),
        },
    }
}

#[allow(
    clippy::cast_possible_truncation,
    reason = "synthetic test PE headers and export names are small constants"
)]
fn test_pe(machine: u16, magic: u16, exports: &[&str]) -> Vec<u8> {
    let mut bytes = vec![0_u8; 0x1000];
    bytes[0..2].copy_from_slice(b"MZ");
    bytes[0x3c..0x40].copy_from_slice(&0x80_u32.to_le_bytes());
    bytes[0x80..0x84].copy_from_slice(b"PE\0\0");
    bytes[0x84..0x86].copy_from_slice(&machine.to_le_bytes());
    bytes[0x86..0x88].copy_from_slice(&1_u16.to_le_bytes());
    bytes[0x94..0x96].copy_from_slice(&0xf0_u16.to_le_bytes());
    let optional = 0x98;
    bytes[optional..optional + 2].copy_from_slice(&magic.to_le_bytes());
    let directory = if magic == 0x10b { 96 } else { 112 };
    let directory_count = if magic == 0x10b { 92 } else { 108 };
    bytes[optional + directory_count..optional + directory_count + 4]
        .copy_from_slice(&16_u32.to_le_bytes());
    bytes[optional + 60..optional + 64].copy_from_slice(&0x200_u32.to_le_bytes());
    bytes[optional + directory..optional + directory + 4]
        .copy_from_slice(&0x1000_u32.to_le_bytes());
    bytes[optional + directory + 4..optional + directory + 8]
        .copy_from_slice(&0x500_u32.to_le_bytes());
    let section = optional + 0xf0;
    bytes[section + 8..section + 12].copy_from_slice(&0x800_u32.to_le_bytes());
    bytes[section + 12..section + 16].copy_from_slice(&0x1000_u32.to_le_bytes());
    bytes[section + 16..section + 20].copy_from_slice(&0x800_u32.to_le_bytes());
    bytes[section + 20..section + 24].copy_from_slice(&0x200_u32.to_le_bytes());
    let export_directory = 0x200;
    bytes[export_directory + 24..export_directory + 28]
        .copy_from_slice(&(exports.len() as u32).to_le_bytes());
    bytes[export_directory + 32..export_directory + 36].copy_from_slice(&0x1040_u32.to_le_bytes());
    let mut string_rva = 0x1040_u32 + (exports.len() as u32) * 4;
    let mut string_offset = 0x240_usize + exports.len() * 4;
    for (i, name) in exports.iter().enumerate() {
        let entry_offset = 0x240 + i * 4;
        bytes[entry_offset..entry_offset + 4].copy_from_slice(&string_rva.to_le_bytes());
        bytes[string_offset..string_offset + name.len()].copy_from_slice(name.as_bytes());
        bytes[string_offset + name.len()] = 0;
        string_rva += name.len() as u32 + 1;
        string_offset += name.len() + 1;
    }
    bytes
}

#[test]
fn required_adapter_payload_matches_expected_files() {
    fn unique_set<'a>(name: &str, slice: &'a [&'a str]) -> BTreeSet<&'a str> {
        let set: BTreeSet<&'a str> = slice.iter().copied().collect();
        assert_eq!(
            set.len(),
            slice.len(),
            "payload list '{name}' contains duplicate entries"
        );
        set
    }

    assert_eq!(
        required_adapter_payload(Product::Foundation),
        &[] as &[&str]
    );
    assert_eq!(
        unique_set(
            "required(Fallout4)",
            required_adapter_payload(Product::Fallout4)
        ),
        unique_set("EXPECTED_FALLOUT4_PAYLOAD", EXPECTED_FALLOUT4_PAYLOAD)
    );
    assert_eq!(
        unique_set(
            "required(SkyrimSe)",
            required_adapter_payload(Product::SkyrimSe)
        ),
        unique_set("EXPECTED_SKYRIM_SE_PAYLOAD", EXPECTED_SKYRIM_SE_PAYLOAD)
    );
    assert_eq!(
        unique_set(
            "required(SkyrimLe)",
            required_adapter_payload(Product::SkyrimLe)
        ),
        unique_set("EXPECTED_SKYRIM_LE_PAYLOAD", EXPECTED_SKYRIM_LE_PAYLOAD)
    );
}

#[test]
fn validate_payload_shape_rejects_missing_each_required_file_individually() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    for (product, architecture, expected) in [
        (
            Product::Fallout4,
            Architecture::X64,
            EXPECTED_FALLOUT4_PAYLOAD,
        ),
        (
            Product::SkyrimSe,
            Architecture::X64,
            EXPECTED_SKYRIM_SE_PAYLOAD,
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            EXPECTED_SKYRIM_LE_PAYLOAD,
        ),
    ] {
        let expectation = test_expectation(product, architecture);

        for missing in expected {
            let files = expected
                .iter()
                .filter(|path| **path != *missing)
                .map(|path| ((*path).to_owned(), 100_u64, "hash".to_owned()))
                .collect::<Vec<_>>();
            let err = match validate_payload_shape(temporary.path(), &expectation, &files) {
                Ok(()) => anyhow::bail!(
                    "for product {product:?} missing {missing}, expected error but got Ok"
                ),
                Err(err) => err.to_string(),
            };
            let expected_msg = format!("adapter package is missing {missing}");
            assert!(
                err.contains(&expected_msg),
                "for product {product:?} missing {missing}, expected error containing '{expected_msg}', but got '{err}'"
            );
        }
    }
    Ok(())
}

#[test]
fn validate_payload_shape_rejects_empty_license_or_payload_files() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    for (product, architecture, expected) in [
        (
            Product::Fallout4,
            Architecture::X64,
            EXPECTED_FALLOUT4_PAYLOAD,
        ),
        (
            Product::SkyrimSe,
            Architecture::X64,
            EXPECTED_SKYRIM_SE_PAYLOAD,
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            EXPECTED_SKYRIM_LE_PAYLOAD,
        ),
    ] {
        let expectation = test_expectation(product, architecture);

        for empty in expected {
            let files = expected
                .iter()
                .map(|path| {
                    let size = if *path == *empty { 0_u64 } else { 100_u64 };
                    ((*path).to_owned(), size, "hash".to_owned())
                })
                .collect::<Vec<_>>();
            let err = match validate_payload_shape(temporary.path(), &expectation, &files) {
                Ok(()) => anyhow::bail!(
                    "for product {product:?} empty {empty}, expected error but got Ok"
                ),
                Err(err) => err.to_string(),
            };
            let expected_msg = format!("adapter package payload file '{empty}' must not be empty");
            assert!(
                err.contains(&expected_msg),
                "for product {product:?} empty {empty}, expected error containing '{expected_msg}', but got '{err}'"
            );
        }
    }
    Ok(())
}

#[test]
fn game_adapter_package_round_trip_verifies_required_payload() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    for (product, architecture, expected, exports, machine, magic) in [
        (
            Product::Fallout4,
            Architecture::X64,
            EXPECTED_FALLOUT4_PAYLOAD,
            &["F4SEPlugin_Load", "F4SEPlugin_Version"][..],
            0x8664_u16,
            0x20b_u16,
        ),
        (
            Product::SkyrimSe,
            Architecture::X64,
            EXPECTED_SKYRIM_SE_PAYLOAD,
            &["SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"][..],
            0x8664_u16,
            0x20b_u16,
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            EXPECTED_SKYRIM_LE_PAYLOAD,
            &["SKSEPlugin_Load", "SKSEPlugin_Query"][..],
            0x014c_u16,
            0x10b_u16,
        ),
    ] {
        let stage = temporary.path().join(format!("{}-stage", product.as_str()));
        fs::create_dir_all(&stage)?;
        for relative in expected {
            let full = stage.join(relative);
            if let Some(parent) = full.parent() {
                fs::create_dir_all(parent)?;
            }
            let path = std::path::Path::new(relative);
            if path
                .extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case("dll"))
            {
                fs::write(&full, test_pe(machine, magic, exports))?;
            } else if path
                .extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case("ini"))
            {
                fs::write(&full, b"[General]\nEnabled=1\n")?;
            } else {
                fs::write(&full, b"License or notice terms content\n")?;
            }
        }

        let expectation = test_expectation(product, architecture);

        let archive = temporary.path().join(package_filename(&expectation));
        write_package_archive(&stage, &archive, &expectation)?;

        let extracted = temporary
            .path()
            .join(format!("{}-extracted", product.as_str()));
        crate::tooling::inspect_archive(&archive, crate::tooling::ArchiveFormat::Zip)?;
        crate::tooling::extract_archive(&archive, crate::tooling::ArchiveFormat::Zip, &extracted)?;

        let inspected = inspect_extracted_package(&extracted, &expectation)?;
        assert_eq!(inspected.entry_count, expected.len() + 1);
        assert_eq!(inspected.exports, exports);
    }
    Ok(())
}

#[test]
fn source_inventory_rejects_build_metadata_and_case_aliases() -> Result<()> {
    assert!(source_forbidden("dependencies/fmt/.git/config"));
    assert!(source_forbidden("nested/build/anything"));
    assert!(!source_forbidden(
        "dependencies/directxtk/build/CompilerAndLinker.cmake"
    ));
    assert!(!source_forbidden(
        "dependencies/directxtk/build/DirectXTK-config.cmake.in"
    ));
    assert!(source_forbidden(
        "dependencies/directxtk/build/CMakeCache.txt"
    ));
    assert!(source_forbidden(
        "dependencies/directxtk/CMakeFiles/generated.cmake"
    ));
    assert!(source_forbidden("dependencies/directxtk/_deps/generated.h"));
    assert!(source_forbidden("dependencies/directxtk/build.ninja"));
    let temp = tempfile::tempdir()?;
    fs::create_dir(temp.path().join("Source"))?;
    if let Err(error) = fs::create_dir(temp.path().join("source")) {
        assert_eq!(error.kind(), std::io::ErrorKind::AlreadyExists);
        assert_eq!(
            fs::canonicalize(temp.path().join("Source"))?,
            fs::canonicalize(temp.path().join("source"))?
        );
        return Ok(());
    }
    fs::write(temp.path().join("Source/a.h"), b"one")?;
    fs::write(temp.path().join("source/a.h"), b"two")?;
    assert!(enumerate_files(temp.path()).is_err());
    Ok(())
}

#[test]
fn serialized_manifest_has_schema_and_sorted_inventory_fields() -> Result<()> {
    let manifest = PackageManifest {
        schema_version: 1,
        product: Product::Foundation,
        architecture: None,
        version: "0.1.0".to_owned(),
        kind: PackageKind::Source,
        source: super::ManifestSourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: Some("a".repeat(64)),
        },
        files: vec![ManifestFile {
            path: "a".to_owned(),
            size: 1,
            sha256: "b".repeat(64),
        }],
    };
    let value = serde_json::to_value(&manifest)?;
    assert_eq!(value["schemaVersion"], 1);
    assert_eq!(value["files"][0]["path"], "a");
    assert_eq!(value["source"]["snapshotSha256"], "a".repeat(64));
    Ok(())
}

#[test]
fn generated_package_manifest_binds_exact_inventory_and_identity() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let stage = temporary.path().join("stage");
    fs::create_dir(&stage)?;
    fs::write(stage.join("README.md"), b"adapter package")?;
    fs::write(stage.join("licenses.txt"), b"license")?;
    let expectation = PackageExpectation {
        product: Product::Foundation,
        architecture: None,
        version: "0.1.0".to_owned(),
        kind: PackageKind::Source,
        source: SourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: Some("a".repeat(64)),
        },
    };
    let archive = temporary.path().join("source.zip");
    write_package_archive(&stage, &archive, &expectation)?;
    let extracted = temporary.path().join("extracted");
    crate::tooling::inspect_archive(&archive, crate::tooling::ArchiveFormat::Zip)?;
    crate::tooling::extract_archive(&archive, crate::tooling::ArchiveFormat::Zip, &extracted)?;
    let inspected = inspect_extracted_package(&extracted, &expectation)?;
    assert_eq!(inspected.entry_count, 3);
    assert_eq!(fs::read(extracted.join("README.md"))?, b"adapter package");

    let wrong_source = PackageExpectation {
        source: SourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: Some("b".repeat(64)),
        },
        ..expectation
    };
    assert!(inspect_extracted_package(&extracted, &wrong_source).is_err());
    let original_hash = crate::tooling::hash_file(&archive)?;
    fs::write(stage.join("manifest.json"), b"supplied manifest")?;
    assert!(write_package_archive(&stage, &archive, &wrong_source).is_err());
    assert_eq!(crate::tooling::hash_file(&archive)?, original_hash);
    Ok(())
}

#[test]
fn package_manifest_rejects_changed_payload_and_unlisted_extra() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let root = temporary.path();
    let expectation = PackageExpectation {
        product: Product::Foundation,
        architecture: None,
        version: "0.1.0".to_owned(),
        kind: PackageKind::Source,
        source: SourceIdentity {
            commit: Some("c".repeat(40)),
            modified: false,
            snapshot_sha256: None,
        },
    };
    fs::write(root.join("README.md"), b"original")?;
    let manifest = PackageManifest {
        schema_version: 1,
        product: Product::Foundation,
        architecture: None,
        version: "0.1.0".to_owned(),
        kind: PackageKind::Source,
        source: super::ManifestSourceIdentity {
            commit: expectation.source.commit.clone(),
            modified: false,
            snapshot_sha256: None,
        },
        files: vec![ManifestFile {
            path: "README.md".to_owned(),
            size: 8,
            sha256: crate::tooling::hash_file(&root.join("README.md"))?,
        }],
    };
    fs::write(root.join("manifest.json"), serde_json::to_vec(&manifest)?)?;
    fs::write(root.join("README.md"), b"modified")?;
    assert!(inspect_extracted_package(root, &expectation).is_err());
    fs::write(root.join("README.md"), b"original")?;
    fs::write(root.join("unexpected.txt"), b"extra")?;
    assert!(inspect_extracted_package(root, &expectation).is_err());
    Ok(())
}

#[test]
fn source_archive_binds_every_authored_tag_data_file_without_git() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let source_root = temporary.path().join("tag-data");
    let package_root = temporary.path().join("package");
    fs::create_dir_all(source_root.join("src"))?;
    fs::create_dir_all(package_root.join("src"))?;
    fs::write(source_root.join("README.md"), b"tag source")?;
    fs::write(source_root.join("src/lib.cpp"), b"implementation")?;
    fs::copy(
        source_root.join("README.md"),
        package_root.join("README.md"),
    )?;
    fs::copy(
        source_root.join("src/lib.cpp"),
        package_root.join("src/lib.cpp"),
    )?;
    let expectation = PackageExpectation {
        product: Product::Foundation,
        architecture: None,
        version: "0.1.0".to_owned(),
        kind: PackageKind::Source,
        source: SourceIdentity {
            commit: Some("d".repeat(40)),
            modified: false,
            snapshot_sha256: None,
        },
    };
    let archive = temporary.path().join("source.zip");
    write_package_archive(&package_root, &archive, &expectation)?;
    let extracted = temporary.path().join("extracted");
    crate::tooling::extract_archive(&archive, crate::tooling::ArchiveFormat::Zip, &extracted)?;
    let mut runner = NoCommands;
    super::validate_source_matches_data_root(&extracted, &expectation, &source_root, &mut runner)?;
    fs::write(source_root.join("src/lib.cpp"), b"changed")?;
    assert!(
        super::validate_source_matches_data_root(
            &extracted,
            &expectation,
            &source_root,
            &mut runner,
        )
        .is_err()
    );
    Ok(())
}
