//! Static runtime-pair verification and exact cold package qualification.

mod cli;
mod execute;
mod input;
mod payload;

use std::num::NonZeroUsize;
use std::path::{Path, PathBuf};

use anyhow::Result;
use clap::{Args, ValueEnum};
use serde::{Deserialize, Serialize};

use crate::model::{
    Architecture, BuildPlan, ExecutionContext, NativeOptions, NativeReport, Product, SourceIdentity,
};
use crate::process::Runner;

/// Whether runtime-pair validation is local-development evidence or release evidence.
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize, ValueEnum)]
#[serde(rename_all = "kebab-case")]
pub enum QualificationMode {
    Development,
    Release,
}

/// Origin of the runtime archives used by static validation.
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum RuntimePairOrigin {
    Local,
    Published,
}

/// One verified runtime archive and its extracted architecture-specific bin directory.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct VerifiedRuntimeArchive {
    pub architecture: Architecture,
    pub path: PathBuf,
    pub sha256: String,
    pub size_bytes: u64,
    pub asset_id: Option<u64>,
    pub asset_name: Option<String>,
    pub bin_directory: PathBuf,
}

/// Pair of statically verified runtime archives and trusted origin identity.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct VerifiedRuntimePair {
    pub schema_version: u32,
    pub origin: RuntimePairOrigin,
    pub repository: Option<String>,
    pub release_id: Option<u64>,
    pub tag: Option<String>,
    pub version: String,
    pub core_sha256: String,
    pub effects_sha256: String,
    pub manifest_sha256: String,
    pub x64: VerifiedRuntimeArchive,
    pub x86: VerifiedRuntimeArchive,
}

/// Result of actually executing exact package cold tests against a verified runtime pair.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct QualificationReport {
    pub schema_version: u32,
    pub product: Product,
    pub architecture: Architecture,
    pub mode: QualificationMode,
    pub source: SourceIdentity,
    pub runtime_pair: VerifiedRuntimePair,
    pub required_tests: Vec<String>,
    pub discovered_tests: Vec<String>,
    pub executed_tests: Vec<String>,
    pub native_report: NativeReport,
    pub release_eligible: bool,
}

/// Static-only result for a dispatch-provided runtime manifest.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct RuntimeInputValidationReport {
    pub schema_version: u32,
    pub status: String,
    pub runtime_pair: VerifiedRuntimePair,
}

/// Validates a runtime input without execution or runs exact native cold cases.
#[derive(Debug, Args)]
pub struct QualifyArgs {
    #[arg(long, default_value = ".")]
    root: PathBuf,
    #[arg(long)]
    toolset: PathBuf,
    #[arg(long, value_enum)]
    product: Product,
    #[arg(long, value_enum)]
    architecture: Architecture,
    #[arg(long)]
    manifest: PathBuf,
    #[arg(long)]
    output: PathBuf,
    #[arg(long)]
    report: PathBuf,
    #[arg(long, value_enum, default_value = "development")]
    mode: QualificationMode,
    #[arg(long)]
    validate_input: bool,
    #[arg(long)]
    jobs: Option<NonZeroUsize>,
}

impl QualifyArgs {
    /// Validates static input or executes the requested package's exact cold tests.
    ///
    /// # Errors
    ///
    /// Returns an error if input validation fails or the native cold qualification does not
    /// complete successfully.
    pub fn run(self, runner: &mut dyn Runner) -> Result<()> {
        cli::run(&self, runner)
    }
}

/// Resolves a local or published runtime manifest, verifies both archives and SDK headers, and
/// extracts each archive into an isolated output directory for later native qualification.
///
/// # Errors
///
/// Returns an error if the manifest, published release identity, archive hashes, package
/// metadata, architecture, or SDK header contents do not match the qualified input.
pub fn inspect_runtime_pair(
    manifest_path: &Path,
    data_root: &Path,
    context: &ExecutionContext,
    mode: QualificationMode,
    runner: &mut dyn Runner,
) -> Result<VerifiedRuntimePair> {
    input::inspect_runtime_pair(manifest_path, data_root, context, mode, runner)
}

/// Runs the exact product cold-test list with one architecture of a verified runtime pair.
///
/// # Errors
///
/// Returns an error if the requested plan is not the exact required cold-test list, the native
/// tests fail, or the resulting report does not match the verified runtime pair.
pub fn qualify_runtime_pair(
    pair: &VerifiedRuntimePair,
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
    runner: &mut dyn Runner,
) -> Result<QualificationReport> {
    execute::qualify_runtime_pair(pair, plan, context, required_tests, options, runner)
}

#[cfg(test)]
mod tests;
