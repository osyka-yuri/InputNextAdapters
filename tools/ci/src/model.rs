//! Shared product, build, source, and native execution models.

use std::num::NonZeroUsize;
use std::path::PathBuf;

use clap::ValueEnum;
use serde::{Deserialize, Serialize};

use crate::tooling::Toolset;

/// A separately versioned package produced by this repository.
#[derive(
    Clone, Copy, Debug, Deserialize, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, ValueEnum,
)]
#[serde(rename_all = "kebab-case")]
pub enum Product {
    Foundation,
    SkyrimSe,
    SkyrimLe,
    Fallout4,
}

impl Product {
    /// Returns the product target built by the extracted-source consumer.
    #[must_use]
    pub const fn source_consumer_target(self) -> &'static str {
        match self {
            Self::Foundation => "inputnext_adapters_effects",
            Self::SkyrimSe => "InputNextSkyrim",
            Self::SkyrimLe => "InputNextSkyrimLE",
            Self::Fallout4 => "InputNextFallout4",
        }
    }

    /// Returns the stable CLI, `CMake`, and report spelling for this product.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Foundation => "foundation",
            Self::SkyrimSe => "skyrim-se",
            Self::SkyrimLe => "skyrim-le",
            Self::Fallout4 => "fallout4",
        }
    }

    /// Returns the source root used by the product's standalone `CMake` project.
    #[must_use]
    pub fn source_directory(self, repository: &std::path::Path) -> PathBuf {
        match self {
            Self::Foundation => repository.to_path_buf(),
            Self::SkyrimSe => repository.join("adapters/skyrim_se"),
            Self::SkyrimLe => repository.join("adapters/skyrim_le"),
            Self::Fallout4 => repository.join("adapters/fallout4"),
        }
    }
}

/// A native ABI supported by a product.
#[derive(
    Clone, Copy, Debug, Deserialize, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, ValueEnum,
)]
#[serde(rename_all = "kebab-case")]
pub enum Architecture {
    X64,
    X86,
}

impl Architecture {
    /// Returns the stable CLI and report spelling for this architecture.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::X64 => "x64",
            Self::X86 => "x86",
        }
    }
}

/// A supported native build or verification profile.
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize, ValueEnum)]
#[serde(rename_all = "kebab-case")]
pub enum Check {
    Tests,
    Quality,
    Analyze,
    Asan,
    Ubsan,
    ClangTests,
    NativeTests,
    NativeQuality,
    NativeAnalyze,
    Package,
}

impl Check {
    /// Returns the stable CLI and report spelling for this check.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Tests => "tests",
            Self::Quality => "quality",
            Self::Analyze => "analyze",
            Self::Asan => "asan",
            Self::Ubsan => "ubsan",
            Self::ClangTests => "clang-tests",
            Self::NativeTests => "native-tests",
            Self::NativeQuality => "native-quality",
            Self::NativeAnalyze => "native-analyze",
            Self::Package => "package",
        }
    }
}

/// Identity of the exact source snapshot used for a build.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct SourceIdentity {
    /// Commit hash when the repository has a commit; absent for an unborn repository.
    pub commit: Option<String>,
    /// Whether tracked or non-ignored untracked files differ from a clean checkout.
    pub modified: bool,
    /// SHA256 of the complete authored source snapshot when modified or unborn.
    pub snapshot_sha256: Option<String>,
}

/// Product, architecture, and check that define one independent build.
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct BuildKey {
    pub product: Product,
    pub architecture: Architecture,
    pub check: Check,
}

/// Fully resolved build directories and test requirements for one build key.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct BuildPlan {
    pub key: BuildKey,
    /// Existing product-owned `CMake` configure preset selected by the planner.
    pub preset: String,
    pub source_directory: PathBuf,
    /// Explicit, isolated directory beneath the host output root.
    pub build_directory: PathBuf,
    /// Exact `CTest` identities that must be present and pass for this plan.
    pub required_tests: Vec<String>,
}

/// Typed native-only configuration accepted by the build executor.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct NativeOptions {
    /// Extracted, architecture-matched `InputNext` runtime package `bin` directory.
    pub runtime_directory: Option<PathBuf>,
    /// Extracted, statically verified plugin DLL used by package probes.
    pub package_plugin: Option<PathBuf>,
    /// Manifest path inside an outer-hash-verified, extracted source bundle.
    pub source_bundle: Option<PathBuf>,
    /// Require a fresh release build directory and reject source overrides.
    pub release_build: bool,
}

/// Host paths and verified tools shared by native, package, and qualification work.
#[derive(Clone, Debug)]
pub struct ExecutionContext {
    pub repository: PathBuf,
    pub output: PathBuf,
    pub jobs: NonZeroUsize,
    pub toolset: Toolset,
    /// Path to the compiler provenance report; its implementation remains private to tooling.
    pub compiler_report: Option<PathBuf>,
    pub source: SourceIdentity,
}

/// Result state recorded for a native command or complete plan.
#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum NativeStatus {
    Passed,
    Failed,
    Skipped,
}

/// One checked configure, build, or test process with durable log locations.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct NativeStep {
    pub name: String,
    pub status: NativeStatus,
    pub exit_code: Option<i32>,
    pub stdout_log: PathBuf,
    pub stderr_log: PathBuf,
    pub diagnostic: Option<String>,
}

/// Typed record of plan execution and its exact runtime-package test evidence.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct NativeReport {
    pub schema_version: u32,
    pub key: BuildKey,
    pub source: SourceIdentity,
    pub status: NativeStatus,
    pub required_tests: Vec<String>,
    pub discovered_tests: Vec<String>,
    pub executed_tests: Vec<String>,
    pub steps: Vec<NativeStep>,
    pub junit_path: Option<PathBuf>,
    pub sarif_paths: Vec<PathBuf>,
    pub diagnostic_paths: Vec<PathBuf>,
    pub failure: Option<String>,
}

/// Returns the exact runtime-package test identities required by each adapter.
#[must_use]
pub fn required_runtime_tests(product: Product) -> &'static [&'static str] {
    match product {
        Product::Foundation => &[
            "runtime_client.production_package",
            "output_session.package.classic_keep_validates_cold",
            "output_session.package.hd_replace_stop_and_clear_validate_cold",
            "output_session.package.trigger_submit_and_clear_validate_cold",
            "output_session.package.close_ack_and_progress_validate_cold",
        ],
        Product::SkyrimSe | Product::SkyrimLe => &[
            "skyrim.package.classic_casting_requests_validate_cold",
            "skyrim.package.hd_and_menu_requests_validate_cold",
            "skyrim.package.focus_cleanup_validates_cold",
        ],
        Product::Fallout4 => &[
            "fallout4.package.classic_requests_validate_cold",
            "fallout4.package.hd_and_vats_requests_validate_cold",
            "fallout4.package.focus_cleanup_validates_cold",
        ],
    }
}

/// Returns the exact static package-load tests for an extracted game plugin.
#[must_use]
pub fn required_plugin_package_tests(product: Product) -> &'static [&'static str] {
    match product {
        Product::Foundation => &[],
        Product::SkyrimSe => &["skyrim_plugin_package"],
        Product::SkyrimLe => &["skyrim_le_load", "skyrim_le_query"],
        Product::Fallout4 => &["fallout4_plugin_package"],
    }
}
