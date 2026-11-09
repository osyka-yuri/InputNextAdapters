//! Static validation of producer reports and package assets.

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};

use crate::model::{
    Architecture, Check, ExecutionContext, NativeReport, NativeStatus, Product, SourceIdentity,
    required_plugin_package_tests, required_runtime_tests,
};
use crate::package::{
    PackageExpectation, PackageKind, PackageMode, PackageReport, PackageReportEntry,
    VerifiedPackage, inspect_package_archive,
};
use crate::process::Runner;
use crate::qualification::{
    QualificationMode, QualificationReport, RuntimePairOrigin, VerifiedRuntimePair,
};

use super::handoff::{MAX_REPORT_BYTES, read_bounded_regular_file};
use super::{AdmittedAsset, VerifiedAsset};
use crate::release::identity::{
    ReleaseIdentity, package_filename, parse_architecture, parse_product,
};

pub(super) fn expected_handoff_files(
    product: Product,
    architecture: Architecture,
    version: &str,
    has_packages: bool,
) -> BTreeSet<String> {
    let mut files = BTreeSet::from([
        "qualification-report.json".to_owned(),
        "runtime-manifest.json".to_owned(),
    ]);
    if has_packages {
        files.extend([
            "native-report.json".to_owned(),
            "package-report.json".to_owned(),
            package_filename(product, Some(architecture), version, PackageKind::Binary),
            package_filename(product, Some(architecture), version, PackageKind::Symbols),
            package_filename(product, None, version, PackageKind::Source),
        ]);
    }
    files
}

pub(super) fn parse_native_report(path: &Path) -> Result<NativeReport> {
    let bytes = read_bounded_regular_file(path, MAX_REPORT_BYTES, "native report")?;
    crate::json::parse(&bytes).context("native report has an invalid or duplicate-key schema")
}

pub(super) fn validate_native_report(
    report: &NativeReport,
    product: Product,
    architecture: Architecture,
    source: &SourceIdentity,
) -> Result<()> {
    ensure!(
        report.schema_version == 1,
        "unsupported native report schema"
    );
    ensure!(
        report.key.product == product
            && report.key.architecture == architecture
            && report.key.check == Check::Package,
        "native report build identity differs from its artifact row"
    );
    ensure!(
        report.source == *source,
        "native report source differs from the qualified tag source"
    );
    ensure!(
        report.status == NativeStatus::Passed,
        "native package report did not pass"
    );
    let expected = required_plugin_package_tests(product)
        .iter()
        .map(|test| (*test).to_owned())
        .collect::<Vec<_>>();
    ensure!(
        report.required_tests == expected,
        "native report required package tests differ from the product contract"
    );
    validate_exact_tests(
        &report.required_tests,
        &report.discovered_tests,
        &report.executed_tests,
        "native package report",
    )?;
    validate_native_steps(report)?;
    Ok(())
}

pub(super) fn validate_qualification_report(
    report: &QualificationReport,
    product: Product,
    architecture: Architecture,
    source: &SourceIdentity,
    runtime: &VerifiedRuntimePair,
) -> Result<()> {
    ensure!(
        report.schema_version == 1,
        "unsupported runtime qualification report schema"
    );
    ensure!(
        report.product == product
            && report.architecture == architecture
            && report.mode == QualificationMode::Release,
        "runtime qualification identity differs from its artifact row"
    );
    ensure!(
        report.source == *source && report.native_report.source == *source,
        "runtime qualification source differs from the qualified tag source"
    );
    ensure!(
        report.native_report.status == NativeStatus::Passed,
        "runtime qualification native tests did not pass"
    );
    ensure!(
        report.native_report.key.product == product
            && report.native_report.key.architecture == architecture
            && report.native_report.key.check == Check::Tests,
        "runtime qualification native report identity differs from its artifact row"
    );
    ensure!(
        report.runtime_pair.origin == RuntimePairOrigin::Published,
        "runtime qualification used a local runtime pair"
    );
    ensure!(
        same_runtime_identity(&report.runtime_pair, runtime),
        "runtime qualification pair differs from independently verified published runtime"
    );
    let expected = required_runtime_tests(product)
        .iter()
        .map(|test| (*test).to_owned())
        .collect::<Vec<_>>();
    ensure!(
        report.required_tests == expected,
        "runtime qualification required cases differ from the product contract"
    );
    ensure!(
        report.native_report.required_tests == expected,
        "runtime qualification native required cases differ from its qualification report"
    );
    validate_exact_tests(
        &report.required_tests,
        &report.discovered_tests,
        &report.executed_tests,
        "runtime qualification report",
    )?;
    validate_exact_tests(
        &report.native_report.required_tests,
        &report.native_report.discovered_tests,
        &report.native_report.executed_tests,
        "native runtime qualification report",
    )?;
    validate_native_steps(&report.native_report)?;
    Ok(())
}

pub(super) fn validate_exact_tests(
    required: &[String],
    discovered: &[String],
    executed: &[String],
    label: &str,
) -> Result<()> {
    let required_count = required.len();
    let required = required.iter().collect::<BTreeSet<_>>();
    let discovered_set = discovered.iter().collect::<BTreeSet<_>>();
    let executed_set = executed.iter().collect::<BTreeSet<_>>();
    ensure!(
        required.len() == required_count
            && required.len() == discovered.len()
            && required.len() == executed.len()
            && discovered_set == required
            && executed_set == required,
        "{label} discovered or executed tests differ from the exact required set"
    );
    Ok(())
}

fn validate_native_steps(report: &NativeReport) -> Result<()> {
    ensure!(
        !report.steps.is_empty(),
        "native evidence contains no process steps"
    );
    ensure!(
        report.failure.is_none(),
        "passed native evidence contains a failure diagnostic"
    );
    ensure!(
        report
            .steps
            .iter()
            .all(|step| { step.status == NativeStatus::Passed && step.exit_code == Some(0) }),
        "native evidence contains a skipped or failed process step"
    );
    Ok(())
}

pub(super) fn same_runtime_identity(
    left: &VerifiedRuntimePair,
    right: &VerifiedRuntimePair,
) -> bool {
    left.schema_version == right.schema_version
        && left.origin == right.origin
        && left.repository == right.repository
        && left.release_id == right.release_id
        && left.tag == right.tag
        && left.version == right.version
        && left.core_sha256 == right.core_sha256
        && left.effects_sha256 == right.effects_sha256
        && left.manifest_sha256 == right.manifest_sha256
        && left.x64.architecture == right.x64.architecture
        && left.x64.sha256 == right.x64.sha256
        && left.x64.size_bytes == right.x64.size_bytes
        && left.x64.asset_id == right.x64.asset_id
        && left.x64.asset_name == right.x64.asset_name
        && left.x86.architecture == right.x86.architecture
        && left.x86.sha256 == right.x86.sha256
        && left.x86.size_bytes == right.x86.size_bytes
        && left.x86.asset_id == right.x86.asset_id
        && left.x86.asset_name == right.x86.asset_name
}

pub(super) fn validate_package_report(
    report: &PackageReport,
    report_value: &serde_json::Value,
    product: Product,
    architecture: Architecture,
    version: &str,
    source: &SourceIdentity,
) -> Result<()> {
    ensure!(
        report.schema_version == 1,
        "unsupported package report schema"
    );
    ensure!(
        report.product == product && report.architecture == architecture,
        "package report identity differs from its artifact row"
    );
    ensure!(
        report.version == version && report.mode == PackageMode::Release,
        "package report version or mode is not release-qualified"
    );
    ensure!(
        report.source == *source,
        "package report source differs from the qualified tag source"
    );
    ensure!(
        report.packages.len() == 3,
        "package report does not contain exactly binary, symbols, and source packages"
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
            "package report package kinds are incomplete or duplicated"
        );
    }

    let binary = report
        .packages
        .iter()
        .find(|entry| entry.kind == PackageKind::Binary)
        .context("package report lacks its binary package")?;
    let source_package = report
        .packages
        .iter()
        .find(|entry| entry.kind == PackageKind::Source)
        .context("package report lacks its source package")?;
    validate_plugin_probe(product, &report.packages, binary)?;
    validate_package_consumers(report_value, product, binary, source_package)?;
    Ok(())
}

fn validate_plugin_probe(
    product: Product,
    packages: &[PackageReportEntry],
    binary: &PackageReportEntry,
) -> Result<()> {
    let expected = required_plugin_package_tests(product)
        .iter()
        .map(|test| (*test).to_owned())
        .collect::<Vec<_>>();
    match (&binary.plugin_probe, expected.is_empty()) {
        (None, true) => {}
        (Some(probe), false) => {
            ensure!(
                probe.package_archive_sha256 == binary.archive_sha256,
                "plugin package probe does not bind the binary archive hash"
            );
            validate_exact_tests(
                &probe.required_tests,
                &probe.discovered_tests,
                &probe.executed_tests,
                "plugin package probe",
            )?;
            ensure!(
                probe.required_tests.iter().collect::<BTreeSet<_>>()
                    == expected.iter().collect::<BTreeSet<_>>(),
                "plugin package probe tests differ from the product contract"
            );
            ensure!(
                probe.cleanup_status == NativeStatus::Passed,
                "plugin package probe cleanup did not pass"
            );
        }
        _ => anyhow::bail!("plugin package probe presence does not match the product"),
    }
    ensure!(
        packages
            .iter()
            .filter(|entry| entry.kind != PackageKind::Binary)
            .all(|entry| entry.plugin_probe.is_none()),
        "plugin package probe is attached to a non-binary archive"
    );
    Ok(())
}

fn validate_package_consumers(
    report_value: &serde_json::Value,
    product: Product,
    binary: &PackageReportEntry,
    source_package: &PackageReportEntry,
) -> Result<()> {
    let expected = if product == Product::Foundation {
        vec![
            (
                "installed-relocated-consumer",
                binary.archive_sha256.as_str(),
                "inputnext_consumer",
            ),
            (
                "extracted-source-consumer",
                source_package.archive_sha256.as_str(),
                product.source_consumer_target(),
            ),
        ]
    } else {
        vec![(
            "extracted-source-consumer",
            source_package.archive_sha256.as_str(),
            product.source_consumer_target(),
        )]
    };
    let checks = report_value
        .get("consumerChecks")
        .and_then(serde_json::Value::as_array)
        .context("package report consumer checks are missing or invalid")?;
    ensure!(
        checks.len() == expected.len(),
        "package report consumer checks are incomplete or unexpected"
    );
    for (kind, archive_sha256, target) in expected {
        let matches = checks
            .iter()
            .filter(|evidence| {
                evidence.get("kind").and_then(serde_json::Value::as_str) == Some(kind)
            })
            .collect::<Vec<_>>();
        ensure!(
            matches.len() == 1,
            "package report must contain exactly one {kind} check"
        );
        validate_consumer_check(matches[0], kind, archive_sha256, target)?;
    }
    Ok(())
}

fn validate_consumer_check(
    evidence: &serde_json::Value,
    kind: &str,
    archive_sha256: &str,
    target: &str,
) -> Result<()> {
    let built_targets = evidence
        .get("builtTargets")
        .and_then(serde_json::Value::as_array)
        .context("package report consumer builtTargets is missing or invalid")?;
    ensure!(
        evidence
            .get("archiveSha256")
            .and_then(serde_json::Value::as_str)
            == Some(archive_sha256)
            && evidence
                .get("configured")
                .and_then(serde_json::Value::as_bool)
                == Some(true)
            && built_targets.len() == 1
            && built_targets[0].as_str() == Some(target)
            && evidence.get("diagnostic") == Some(&serde_json::Value::Null),
        "package report {kind} check is not bound to its verified archive and target"
    );
    Ok(())
}

#[allow(
    clippy::too_many_arguments,
    reason = "each input binds the package archive to its source, row, runtime tools, and process runner"
)]
pub(super) fn validate_packages(
    files: &BTreeMap<String, PathBuf>,
    report: &PackageReport,
    product: Product,
    architecture: Architecture,
    version: &str,
    source: &SourceIdentity,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<Vec<VerifiedPackage>> {
    let mut verified = Vec::new();
    for entry in &report.packages {
        let arch = (entry.kind != PackageKind::Source).then_some(architecture);
        let expected_name = package_filename(product, arch, version, entry.kind);
        ensure!(
            entry.name == expected_name,
            "package report contains an unexpected archive name"
        );
        let path = files
            .get(&entry.name)
            .context("package report archive is missing from the artifact")?;
        let expectation = PackageExpectation {
            product,
            architecture: arch,
            version: version.to_owned(),
            kind: entry.kind,
            source: source.clone(),
        };
        let package = inspect_package_archive(path, &expectation, context, runner)?;
        ensure!(
            package.archive_sha256 == entry.archive_sha256,
            "package report archive hash differs from the verified ZIP"
        );
        ensure!(
            package.manifest_sha256 == entry.manifest_sha256,
            "package report manifest hash differs from the verified ZIP"
        );
        ensure!(
            package.entry_count == entry.entry_count,
            "package report entry count differs from the verified ZIP"
        );
        ensure!(
            std::fs::metadata(path)?.len() == entry.size_bytes,
            "package report size differs from the verified ZIP"
        );
        ensure!(
            package.product == product
                && package.architecture == arch
                && package.version == version
                && package.kind == entry.kind
                && package.source == *source,
            "verified package identity differs from its report"
        );
        verified.push(package);
    }
    Ok(verified)
}

pub(super) fn add_verified_package(
    assets: &mut BTreeMap<String, AdmittedAsset>,
    package: VerifiedPackage,
    artifact_id: u64,
) -> Result<()> {
    let name = package
        .archive_path
        .file_name()
        .context("verified package path has no file name")?
        .to_str()
        .context("verified package filename is not valid UTF-8")?
        .to_owned();
    let size_bytes = std::fs::metadata(&package.archive_path)?.len();
    let asset = VerifiedAsset {
        name: name.clone(),
        sha256: package.archive_sha256.clone(),
        size_bytes,
        artifact_id,
    };
    if let Some(previous) = assets.get(&name) {
        ensure!(
            previous.asset.sha256 == asset.sha256
                && previous.asset.size_bytes == asset.size_bytes
                && previous.manifest_sha256 == package.manifest_sha256,
            "duplicate package asset name has different content"
        );
        return Ok(());
    }
    assets.insert(
        name,
        AdmittedAsset {
            asset,
            path: package.archive_path,
            manifest_sha256: package.manifest_sha256,
        },
    );
    Ok(())
}

pub(super) fn parse_artifact_row(
    name: &str,
    identity: &ReleaseIdentity,
) -> Result<(Product, Architecture)> {
    let suffix = format!("-{}-{}", identity.run_id, identity.run_attempt);
    let row = name
        .strip_prefix("release-")
        .and_then(|value| value.strip_suffix(&suffix))
        .context("artifact name does not match the current run and attempt")?;
    let (product, architecture) = row
        .rsplit_once('-')
        .context("artifact name lacks an architecture")?;
    Ok((parse_product(product)?, parse_architecture(architecture)?))
}

#[cfg(test)]
mod tests;
