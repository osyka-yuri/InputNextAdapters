//! Package archive inspection, validation, deduplication, and signing preparation.

use std::collections::BTreeMap;
use std::path::Path;

use anyhow::{Context, Result, ensure};

use crate::model::{Architecture, Product};
use crate::package::{PackageKind, PackageMode, PackageReport, PackageReportEntry};

use super::identity::{
    ReleaseIdentity, package_filename, package_rows, parse_architecture, parse_product,
};
use super::verify::handoff::{MAX_REPORT_BYTES, read_bounded_regular_file};

#[cfg(test)]
mod tests;

pub(super) fn prepare_signing_packages(
    identity: &ReleaseIdentity,
    handoff: &Path,
    out_dir: &Path,
) -> Result<usize> {
    ensure!(handoff.is_absolute(), "handoff path must be absolute");
    ensure!(out_dir.is_absolute(), "out_dir path must be absolute");
    let handoff = dunce::canonicalize(handoff).context("resolve handoff directory")?;
    std::fs::create_dir_all(out_dir).context("create signing packages directory")?;
    let out_dir = dunce::canonicalize(out_dir).context("resolve signing packages directory")?;
    let existing_entries = std::fs::read_dir(&out_dir)
        .context("read signing packages directory")?
        .collect::<std::result::Result<Vec<_>, _>>()
        .context("list signing packages directory")?;
    ensure!(
        existing_entries.is_empty(),
        "signing packages destination directory must be empty: {}",
        out_dir.display()
    );

    let mut copied_packages: BTreeMap<String, String> = BTreeMap::new();
    let rows = package_rows(identity.product);
    for (product_str, arch_str) in &rows {
        let product = parse_product(product_str)?;
        let architecture = parse_architecture(arch_str)?;
        let dir_name = format!(
            "release-{product_str}-{arch_str}-{}-{}",
            identity.run_id, identity.run_attempt
        );
        let named_dir = handoff.join(&dir_name);
        let flat_layout =
            rows.len() == 1 && !named_dir.is_dir() && handoff.join("package-report.json").is_file();
        let row_dir = if flat_layout {
            handoff.as_path()
        } else {
            named_dir.as_path()
        };
        ensure!(
            row_dir.is_dir(),
            "missing expected handoff directory {}",
            row_dir.display()
        );
        let package_report_path = row_dir.join("package-report.json");
        let report_bytes =
            read_bounded_regular_file(&package_report_path, MAX_REPORT_BYTES, "package report")?;
        let report: PackageReport =
            crate::json::parse(&report_bytes).context("invalid package report")?;
        validate_signing_package_report(&report, product, architecture, identity)?;

        for entry in &report.packages {
            let expected_arch = (entry.kind != PackageKind::Source).then_some(architecture);
            let expected_name =
                package_filename(product, expected_arch, &identity.version, entry.kind);
            ensure!(
                entry.name == expected_name,
                "package report entry name '{}' does not match expected filename '{}' for kind {:?}",
                entry.name,
                expected_name,
                entry.kind
            );
            copy_verified_package(entry, row_dir, &out_dir, &mut copied_packages)?;
        }
    }
    ensure!(
        !copied_packages.is_empty(),
        "no package archives found to sign"
    );
    Ok(copied_packages.len())
}

fn copy_verified_package(
    entry: &PackageReportEntry,
    row_dir: &Path,
    out_dir: &Path,
    copied_packages: &mut BTreeMap<String, String>,
) -> Result<()> {
    validate_package_filename(&entry.name)?;
    let zip_path = row_dir.join(&entry.name);
    let metadata = std::fs::symlink_metadata(&zip_path)
        .with_context(|| format!("could not inspect package zip {}", zip_path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "package zip must be a regular non-symlink file: {}",
        zip_path.display()
    );
    ensure!(
        metadata.len() == entry.size_bytes,
        "package zip size differs from report for {}: expected {} bytes, found {} bytes",
        entry.name,
        entry.size_bytes,
        metadata.len()
    );
    let actual_sha = crate::tooling::hash_file(&zip_path)?;
    ensure!(
        actual_sha == entry.archive_sha256,
        "package zip hash differs from report for {}",
        entry.name
    );
    let target_path = out_dir.join(&entry.name);
    if let Some(existing_sha) = copied_packages.get(&entry.name) {
        ensure!(
            *existing_sha == actual_sha,
            "duplicate package archive has conflicting hash: {}",
            entry.name
        );
    } else {
        ensure!(
            !target_path.exists(),
            "target package path unexpectedly already exists: {}",
            target_path.display()
        );
        std::fs::copy(&zip_path, &target_path)
            .with_context(|| format!("could not copy package zip to {}", target_path.display()))?;
        copied_packages.insert(entry.name.clone(), actual_sha);
    }
    Ok(())
}

pub(super) fn validate_package_filename(name: &str) -> Result<()> {
    ensure!(
        !name.contains('/') && !name.contains('\\') && !name.contains(".."),
        "package archive filename contains directory separators or traversal components: '{name}'"
    );
    ensure!(
        Path::new(name)
            .extension()
            .is_some_and(|ext| ext.eq_ignore_ascii_case("zip")),
        "package archive filename must have a zip extension: '{name}'"
    );
    ensure!(
        name.bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"-._".contains(&byte)),
        "package archive filename contains unsafe characters: '{name}'"
    );
    Ok(())
}

fn validate_signing_package_report(
    report: &PackageReport,
    product: Product,
    architecture: Architecture,
    identity: &ReleaseIdentity,
) -> Result<()> {
    ensure!(
        report.schema_version == 1,
        "unsupported package report schema version"
    );
    ensure!(
        report.product == product && report.architecture == architecture,
        "package report identity mismatch"
    );
    ensure!(
        report.version == identity.version,
        "package report version mismatch"
    );
    ensure!(
        report.mode == PackageMode::Release,
        "package report was not produced in release mode"
    );
    ensure!(
        report.source.commit.as_deref() == Some(&identity.source_commit)
            && !report.source.modified
            && report.source.snapshot_sha256.is_none(),
        "package report source identity differs from qualified release tag source"
    );
    ensure!(
        report.packages.len() == 3,
        "package report must contain exactly 3 packages (binary, symbols, source)"
    );
    for kind in [
        PackageKind::Binary,
        PackageKind::Symbols,
        PackageKind::Source,
    ] {
        ensure!(
            report
                .packages
                .iter()
                .filter(|entry| entry.kind == kind)
                .count()
                == 1,
            "package report must contain exactly one package of kind {kind:?}"
        );
    }
    Ok(())
}
