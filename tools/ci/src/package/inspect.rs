//! Static archive verification and package probe execution.

use std::fs;
use std::path::Path;

use anyhow::{Context, Result, ensure};

use crate::model::{
    Check, ExecutionContext, NativeOptions, NativeStatus, required_plugin_package_tests,
};
use crate::plan::create_plan;
use crate::process::Runner;

use super::archive;
use super::dependencies;
use super::{PackageExpectation, PackageKind, PackageMode, PluginProbeEvidence, VerifiedPackage};
/// Reopens a package archive and verifies identity, exact inventory, payload hashes, and PE exports.
///
/// # Errors
///
/// Returns an error if the archive is malformed, its identity or payload inventory differs from
/// the expectation, its source payload differs from the supplied source tree, or its native PE
/// image fails the architecture and export checks.
pub fn inspect_package_archive(
    archive_path: &Path,
    expectation: &PackageExpectation,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<VerifiedPackage> {
    let archive_path = dunce::canonicalize(archive_path).with_context(|| {
        format!(
            "could not resolve package archive {}",
            archive_path.display()
        )
    })?;
    let metadata = fs::symlink_metadata(&archive_path).with_context(|| {
        format!(
            "could not inspect package archive {}",
            archive_path.display()
        )
    })?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= 3 * 1024 * 1024 * 1024,
        "package archive must be a bounded regular file"
    );
    let archive_parent = archive_path
        .parent()
        .context("package archive has no parent")?;
    let extraction = tempfile::TempDir::new_in(archive_parent)
        .context("could not create isolated package verification directory")?;
    crate::tooling::inspect_archive(&archive_path, crate::tooling::ArchiveFormat::Zip)?;
    crate::tooling::extract_archive(
        &archive_path,
        crate::tooling::ArchiveFormat::Zip,
        extraction.path(),
    )?;
    let extracted = archive::inspect_extracted_package(extraction.path(), expectation)?;
    if expectation.kind == PackageKind::Source {
        dependencies::validate_source_bundle_manifest(extraction.path(), expectation.product)?;
        archive::validate_source_matches_data_root(
            extraction.path(),
            expectation,
            &context.repository,
            runner,
        )?;
    }
    let manifest_path = extraction.path().join("manifest.json");
    let manifest_sha256 = crate::tooling::hash_file(&manifest_path)?;
    let archive_sha256 = crate::tooling::hash_file(&archive_path)?;
    Ok(VerifiedPackage {
        schema_version: 1,
        product: expectation.product,
        architecture: expectation.architecture,
        version: expectation.version.clone(),
        kind: expectation.kind,
        source: expectation.source.clone(),
        archive_path,
        archive_sha256,
        manifest_sha256,
        entry_count: extracted.entry_count,
        pe_machine: extracted.pe_machine,
        exports: extracted.exports,
    })
}

pub(super) fn run_plugin_package_probe(
    archive_path: &Path,
    verified: &VerifiedPackage,
    expectation: &PackageExpectation,
    mode: PackageMode,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<PluginProbeEvidence> {
    let relative = archive::plugin_relative_path(expectation.product)
        .context("adapter package has no fixed plugin path")?;
    let extraction = tempfile::TempDir::new_in(&context.output)
        .context("could not create extracted plugin probe directory")?;
    crate::tooling::extract_archive(
        archive_path,
        crate::tooling::ArchiveFormat::Zip,
        extraction.path(),
    )?;
    let plugin = dunce::canonicalize(extraction.path().join(relative))
        .context("verified package ZIP is missing its plugin DLL")?;
    ensure!(
        plugin.is_file(),
        "verified plugin path is not a regular file"
    );
    let options = NativeOptions {
        release_build: mode == PackageMode::Release,
        ..NativeOptions::default()
    };
    let plan = create_plan(
        &context.repository,
        &context.output,
        expectation.product,
        expectation
            .architecture
            .context("plugin probe requires a package architecture")?,
        Check::Package,
        &options,
    )?;
    let required_tests = required_plugin_package_tests(expectation.product)
        .iter()
        .map(|name| (*name).to_owned())
        .collect::<Vec<_>>();
    let evidence = crate::native::run_package_probe(
        &plan,
        context,
        &required_tests,
        &options,
        &plugin,
        runner,
    )?;
    ensure!(
        evidence.key == plan.key
            && evidence.source == context.source
            && evidence.required_tests == required_tests
            && evidence.cleanup_status == NativeStatus::Passed,
        "native package probe evidence is not bound to the expected package tests"
    );
    let report_path = context.output.join("reports").join(format!(
        "{}-package-probe.json",
        crate::plan::build_key_slug(plan.key)
    ));
    let report_sha256 = crate::tooling::hash_file(&report_path)?;
    Ok(PluginProbeEvidence {
        package_archive_sha256: verified.archive_sha256.clone(),
        required_tests: evidence.required_tests,
        discovered_tests: evidence.discovered_tests,
        executed_tests: evidence.executed_tests,
        cleanup_status: evidence.cleanup_status,
        report_path,
        report_sha256,
    })
}
