//! Public package identities and report contracts.

use std::path::PathBuf;

use clap::ValueEnum;
use serde::{Deserialize, Serialize};

use crate::model::{Architecture, NativeStatus, Product, SourceIdentity};

/// The independently inventoried payload produced for a product.
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize, ValueEnum)]
#[serde(rename_all = "kebab-case")]
pub enum PackageKind {
    Binary,
    Symbols,
    Source,
}

impl PackageKind {
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Binary => "binary",
            Self::Symbols => "symbols",
            Self::Source => "source",
        }
    }
}

/// Identity that a package archive must prove in its embedded manifest.
#[derive(Clone, Debug)]
pub struct PackageExpectation {
    pub product: Product,
    pub architecture: Option<Architecture>,
    pub version: String,
    pub kind: PackageKind,
    pub source: SourceIdentity,
}

/// Static evidence produced after the complete archive and its payload were checked.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct VerifiedPackage {
    pub schema_version: u32,
    pub product: Product,
    pub architecture: Option<Architecture>,
    pub version: String,
    pub kind: PackageKind,
    pub source: SourceIdentity,
    pub archive_path: PathBuf,
    pub archive_sha256: String,
    pub manifest_sha256: String,
    pub entry_count: usize,
    pub pe_machine: Option<String>,
    pub exports: Vec<String>,
}

/// Result of verifying an installed or extracted-source package consumer.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct ConsumerEvidence {
    pub kind: String,
    pub archive_sha256: String,
    pub configured: bool,
    pub built_targets: Vec<String>,
    pub diagnostic: Option<String>,
}

/// Report for all binary, symbol, and source payloads created for one build.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct PackageReport {
    pub schema_version: u32,
    pub product: Product,
    pub architecture: Architecture,
    pub version: String,
    pub mode: PackageMode,
    pub source: SourceIdentity,
    pub packages: Vec<PackageReportEntry>,
    pub consumer_checks: Vec<ConsumerEvidence>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct PackageReportEntry {
    pub kind: PackageKind,
    pub name: String,
    pub size_bytes: u64,
    pub archive_sha256: String,
    pub manifest_sha256: String,
    pub entry_count: usize,
    pub plugin_probe: Option<PluginProbeEvidence>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct PluginProbeEvidence {
    pub package_archive_sha256: String,
    pub required_tests: Vec<String>,
    pub discovered_tests: Vec<String>,
    pub executed_tests: Vec<String>,
    pub cleanup_status: NativeStatus,
    pub report_path: PathBuf,
    pub report_sha256: String,
}

/// Whether package work is ordinary local qualification or is intended for release production.
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize, ValueEnum)]
#[serde(rename_all = "kebab-case")]
pub enum PackageMode {
    Development,
    Release,
}
