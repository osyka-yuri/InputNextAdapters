use std::path::PathBuf;

use anyhow::Result;

use super::{prepare_signing_packages, validate_package_filename};
use crate::model::{Architecture, Product, SourceIdentity};
use crate::package::{PackageKind, PackageMode, PackageReport, PackageReportEntry};
use crate::release::identity::{
    ReleaseIdentity, package_filename, package_rows, parse_architecture, parse_product,
};
use crate::tooling::write_json_report;

fn identity(product: Product) -> ReleaseIdentity {
    let tag_prefix = match product {
        Product::Foundation => "foundation",
        Product::SkyrimSe => "skyrim-se",
        Product::SkyrimLe => "skyrim-le",
        Product::Fallout4 => "fallout4",
    };
    ReleaseIdentity {
        schema_version: 1,
        repository: "example/adapters".into(),
        product,
        version: "1.2.3".into(),
        tag: format!("{tag_prefix}/v1.2.3"),
        source_commit: "b".repeat(40),
        workflow_commit: "a".repeat(40),
        run_id: "123".into(),
        run_attempt: "2".into(),
        notes: "- Current release".into(),
    }
}

fn create_test_handoff(
    product: Product,
    ident: &ReleaseIdentity,
) -> Result<(tempfile::TempDir, PathBuf, PathBuf)> {
    let root = tempfile::tempdir()?;
    let handoff = root.path().join("handoff");
    let out_dir = root.path().join("signing-packages");
    std::fs::create_dir_all(&handoff)?;
    std::fs::create_dir_all(&out_dir)?;

    for (product_str, arch_str) in package_rows(product) {
        let p = parse_product(product_str)?;
        let arch = parse_architecture(arch_str)?;
        let row_dir = handoff.join(format!(
            "release-{product_str}-{arch_str}-{}-{}",
            ident.run_id, ident.run_attempt
        ));
        std::fs::create_dir_all(&row_dir)?;

        let mut entries = Vec::new();
        for kind in [
            PackageKind::Binary,
            PackageKind::Symbols,
            PackageKind::Source,
        ] {
            let expected_arch = (kind != PackageKind::Source).then_some(arch);
            let name = package_filename(p, expected_arch, &ident.version, kind);
            let zip_path = row_dir.join(&name);
            let content = format!("archive content for {name}");
            std::fs::write(&zip_path, content.as_bytes())?;
            let archive_sha256 = crate::tooling::hash_file(&zip_path)?;
            entries.push(PackageReportEntry {
                kind,
                name,
                size_bytes: u64::try_from(content.len())?,
                archive_sha256,
                manifest_sha256: "0".repeat(64),
                entry_count: 1,
                plugin_probe: None,
            });
        }

        let report = PackageReport {
            schema_version: 1,
            product: p,
            architecture: arch,
            version: ident.version.clone(),
            mode: PackageMode::Release,
            source: SourceIdentity {
                commit: Some(ident.source_commit.clone()),
                modified: false,
                snapshot_sha256: None,
            },
            packages: entries,
            consumer_checks: Vec::new(),
        };
        write_json_report(&row_dir.join("package-report.json"), &report)?;
    }

    Ok((root, handoff, out_dir))
}

#[test]
fn prepare_signing_packages_admits_valid_single_row_packages() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let count = prepare_signing_packages(&ident, &handoff, &out_dir)?;
    assert_eq!(count, 3);
    let bin = package_filename(
        Product::Fallout4,
        Some(Architecture::X64),
        &ident.version,
        PackageKind::Binary,
    );
    let sym = package_filename(
        Product::Fallout4,
        Some(Architecture::X64),
        &ident.version,
        PackageKind::Symbols,
    );
    let src = package_filename(Product::Fallout4, None, &ident.version, PackageKind::Source);
    assert!(out_dir.join(bin).is_file());
    assert!(out_dir.join(sym).is_file());
    assert!(out_dir.join(src).is_file());
    Ok(())
}

#[test]
fn prepare_signing_packages_admits_and_deduplicates_multi_row_packages() -> Result<()> {
    let ident = identity(Product::Foundation);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Foundation, &ident)?;
    let count = prepare_signing_packages(&ident, &handoff, &out_dir)?;
    assert_eq!(count, 5);
    let bin_x64 = package_filename(
        Product::Foundation,
        Some(Architecture::X64),
        &ident.version,
        PackageKind::Binary,
    );
    let sym_x64 = package_filename(
        Product::Foundation,
        Some(Architecture::X64),
        &ident.version,
        PackageKind::Symbols,
    );
    let bin_x86 = package_filename(
        Product::Foundation,
        Some(Architecture::X86),
        &ident.version,
        PackageKind::Binary,
    );
    let sym_x86 = package_filename(
        Product::Foundation,
        Some(Architecture::X86),
        &ident.version,
        PackageKind::Symbols,
    );
    let src = package_filename(
        Product::Foundation,
        None,
        &ident.version,
        PackageKind::Source,
    );
    assert!(out_dir.join(bin_x64).is_file());
    assert!(out_dir.join(sym_x64).is_file());
    assert!(out_dir.join(bin_x86).is_file());
    assert!(out_dir.join(sym_x86).is_file());
    assert!(out_dir.join(src).is_file());
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_conflicting_hash_for_shared_package() -> Result<()> {
    let ident = identity(Product::Foundation);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Foundation, &ident)?;
    let x86_row = handoff.join(format!(
        "release-foundation-x86-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let src_name = package_filename(
        Product::Foundation,
        None,
        &ident.version,
        PackageKind::Source,
    );
    let source_zip = x86_row.join(&src_name);
    std::fs::write(&source_zip, b"different content")?;
    let report_path = x86_row.join("package-report.json");
    let mut report: PackageReport = crate::json::parse(&std::fs::read(&report_path)?)?;
    for p in &mut report.packages {
        if p.kind == PackageKind::Source {
            p.size_bytes = u64::try_from(b"different content".len())?;
            p.archive_sha256 = crate::tooling::hash_file(&source_zip)?;
        }
    }
    write_json_report(&report_path, &report)?;
    let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
        anyhow::bail!("expected conflicting hash error");
    };
    assert!(
        err.to_string()
            .contains("duplicate package archive has conflicting hash")
    );
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_non_empty_output_directory() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    std::fs::write(out_dir.join("stray.txt"), b"stray")?;
    let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
        anyhow::bail!("expected non-empty output directory error");
    };
    assert!(err.to_string().contains("must be empty"));
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_symlink_archive() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let target_file = row_dir.join("real_target.zip");
    std::fs::write(&target_file, b"target")?;
    let bin_name = package_filename(
        Product::Fallout4,
        Some(Architecture::X64),
        &ident.version,
        PackageKind::Binary,
    );
    let symlink_path = row_dir.join(&bin_name);
    std::fs::remove_file(&symlink_path)?;
    #[cfg(unix)]
    {
        std::os::unix::fs::symlink(&target_file, &symlink_path)?;
        let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
            anyhow::bail!("expected symlink error");
        };
        assert!(
            err.to_string()
                .contains("must be a regular non-symlink file")
        );
    }
    #[cfg(windows)]
    {
        if std::os::windows::fs::symlink_file(&target_file, &symlink_path).is_ok() {
            let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
                anyhow::bail!("expected symlink error");
            };
            assert!(
                err.to_string()
                    .contains("must be a regular non-symlink file")
            );
        }
    }
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_unsafe_traversal_or_characters_filename() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let report_path = row_dir.join("package-report.json");
    let mut report: PackageReport = crate::json::parse(&std::fs::read(&report_path)?)?;
    report.packages[0].name = "../traversal.zip".into();
    write_json_report(&report_path, &report)?;
    assert!(prepare_signing_packages(&ident, &handoff, &out_dir).is_err());
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_kind_filename_mismatch() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let report_path = row_dir.join("package-report.json");
    let mut report: PackageReport = crate::json::parse(&std::fs::read(&report_path)?)?;
    report.packages[0].kind = PackageKind::Symbols;
    report.packages[1].kind = PackageKind::Binary;
    write_json_report(&report_path, &report)?;
    let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
        anyhow::bail!("expected kind mismatch error");
    };
    assert!(err.to_string().contains("does not match expected filename"));
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_invalid_report_mode_or_commit() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let report_path = row_dir.join("package-report.json");

    let original_report: PackageReport = crate::json::parse(&std::fs::read(&report_path)?)?;

    let mut r1 = original_report.clone();
    r1.schema_version = 2;
    write_json_report(&report_path, &r1)?;
    assert!(prepare_signing_packages(&ident, &handoff, &out_dir).is_err());

    let mut r2 = original_report.clone();
    r2.mode = PackageMode::Development;
    write_json_report(&report_path, &r2)?;
    assert!(prepare_signing_packages(&ident, &handoff, &out_dir).is_err());

    let mut r3 = original_report.clone();
    r3.source.commit = Some("f".repeat(40));
    write_json_report(&report_path, &r3)?;
    assert!(prepare_signing_packages(&ident, &handoff, &out_dir).is_err());

    let mut r4 = original_report;
    r4.source.modified = true;
    write_json_report(&report_path, &r4)?;
    assert!(prepare_signing_packages(&ident, &handoff, &out_dir).is_err());
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_source_snapshot_sha256() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let report_path = row_dir.join("package-report.json");
    let mut report: PackageReport = crate::json::parse(&std::fs::read(&report_path)?)?;
    report.source.snapshot_sha256 = Some("a".repeat(64));
    write_json_report(&report_path, &report)?;
    let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
        anyhow::bail!("expected source snapshot sha256 error");
    };
    assert!(
        err.to_string()
            .contains("package report source identity differs from qualified release tag source")
    );
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_file_size_mismatch() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let report_path = row_dir.join("package-report.json");
    let mut report: PackageReport = crate::json::parse(&std::fs::read(&report_path)?)?;
    report.packages[0].size_bytes += 1;
    write_json_report(&report_path, &report)?;
    let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
        anyhow::bail!("expected size mismatch error");
    };
    assert!(
        err.to_string()
            .contains("package zip size differs from report")
    );
    Ok(())
}

#[test]
fn prepare_signing_packages_admits_flat_single_artifact_handoff_layout() -> Result<()> {
    let ident = identity(Product::Fallout4);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Fallout4, &ident)?;
    let row_dir = handoff.join(format!(
        "release-fallout4-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let entries = std::fs::read_dir(&row_dir)?.collect::<std::io::Result<Vec<_>>>()?;
    for entry in entries {
        std::fs::rename(entry.path(), handoff.join(entry.file_name()))?;
    }
    std::fs::remove_dir(&row_dir)?;

    let count = prepare_signing_packages(&ident, &handoff, &out_dir)?;
    assert_eq!(count, 3);
    for kind in [
        PackageKind::Binary,
        PackageKind::Symbols,
        PackageKind::Source,
    ] {
        let expected_arch = (kind != PackageKind::Source).then_some(Architecture::X64);
        let name = package_filename(Product::Fallout4, expected_arch, &ident.version, kind);
        assert!(out_dir.join(&name).is_file());
    }
    Ok(())
}

#[test]
fn prepare_signing_packages_rejects_flat_layout_for_multi_row_products() -> Result<()> {
    let ident = identity(Product::Foundation);
    let (_root, handoff, out_dir) = create_test_handoff(Product::Foundation, &ident)?;
    let row_dir = handoff.join(format!(
        "release-foundation-x64-{}-{}",
        ident.run_id, ident.run_attempt
    ));
    let entries = std::fs::read_dir(&row_dir)?.collect::<std::io::Result<Vec<_>>>()?;
    for entry in entries {
        std::fs::rename(entry.path(), handoff.join(entry.file_name()))?;
    }
    std::fs::remove_dir(&row_dir)?;

    let Err(err) = prepare_signing_packages(&ident, &handoff, &out_dir) else {
        anyhow::bail!("expected rejection of flat layout for multi-row product");
    };
    assert!(
        err.to_string()
            .contains("missing expected handoff directory")
    );
    Ok(())
}

#[test]
fn validate_package_filename_enforces_safety_and_zip_extension() {
    assert!(validate_package_filename("InputNextAdapters-fallout4-1.2.3-windows-x64.zip").is_ok());
    assert!(validate_package_filename("archive_1.0.0-rc.1.zip").is_ok());

    assert!(validate_package_filename("../traversal.zip").is_err());
    assert!(validate_package_filename("sub/archive.zip").is_err());
    assert!(validate_package_filename("sub\\archive.zip").is_err());
    assert!(validate_package_filename("archive.tar.gz").is_err());
    assert!(validate_package_filename("archive.exe").is_err());
    assert!(validate_package_filename("archive.zip.bak").is_err());
    assert!(validate_package_filename("archive:unsafe.zip").is_err());
    assert!(validate_package_filename("archive*unsafe.zip").is_err());
    assert!(validate_package_filename("archive unsafe.zip").is_err());
}
