//! Toolchain reports and path validation.

use std::collections::BTreeMap;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};

use super::download;

const MAX_REPORT_BYTES: u64 = 4 * 1024 * 1024;

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(super) enum ToolKind {
    Executable,
    Module,
    Archive,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct ToolEntry {
    pub version: String,
    pub path: PathBuf,
    pub kind: ToolKind,
    pub archive: Option<PathBuf>,
    pub sha256: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct ToolchainReport {
    pub schema_version: u32,
    pub profile: String,
    pub platform: String,
    pub lock_sha256: String,
    pub tools: BTreeMap<String, ToolEntry>,
    pub environment: BTreeMap<String, String>,
}

/// Verified paths and environment recorded by a local toolchain setup.
#[derive(Clone, Debug)]
pub struct Toolset {
    pub(super) report: ToolchainReport,
}

impl Toolset {
    /// Returns a verified executable path for a named tool.
    ///
    /// # Errors
    ///
    /// Returns an error if the tool is absent, has another kind, or its path is missing.
    pub fn executable(&self, name: &str) -> Result<&Path> {
        let entry = self
            .report
            .tools
            .get(name)
            .with_context(|| format!("toolset has no '{name}' tool"))?;
        ensure!(
            entry.kind == ToolKind::Executable,
            "tool '{name}' is not an executable"
        );
        ensure!(entry.path.is_file(), "tool '{name}' executable is missing");
        Ok(&entry.path)
    }

    /// Returns environment values recorded by toolchain setup.
    #[must_use]
    pub fn environment(&self) -> &BTreeMap<String, String> {
        &self.report.environment
    }

    /// Returns the digest of the lock that produced this report.
    #[must_use]
    pub fn lock_sha256(&self) -> &str {
        &self.report.lock_sha256
    }

    /// Returns the host platform recorded by this report.
    #[must_use]
    pub fn platform(&self) -> &str {
        &self.report.platform
    }
}

/// Loads a tool report and checks that all recorded paths remain present.
///
/// # Errors
///
/// Returns an error if the report is malformed or any recorded path is missing or invalid.
pub fn load_toolset(path: &Path) -> Result<Toolset> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect toolchain report {}", path.display()))?;
    ensure!(
        metadata.is_file(),
        "toolchain report must be a regular file"
    );
    ensure!(
        !metadata.file_type().is_symlink(),
        "toolchain report must not be a symbolic link"
    );
    ensure!(
        metadata.len() <= MAX_REPORT_BYTES,
        "toolchain report exceeds the size limit"
    );
    let bytes = fs::read(path).with_context(|| format!("could not read {}", path.display()))?;
    let report: ToolchainReport = crate::json::parse(&bytes)
        .with_context(|| format!("toolchain report {} is not valid JSON", path.display()))?;
    ensure!(
        report.schema_version == 1,
        "unsupported toolchain report schema"
    );
    download::validate_sha256(&report.lock_sha256)?;
    ensure!(
        matches!(report.platform.as_str(), "windows-x64" | "linux-x64"),
        "toolchain report has an unsupported platform"
    );
    for (name, entry) in &report.tools {
        ensure!(
            entry.path.is_absolute(),
            "toolchain path for '{name}' is not absolute"
        );
        let metadata = fs::symlink_metadata(&entry.path)
            .with_context(|| format!("tool '{name}' path is missing"))?;
        ensure!(
            metadata.is_file(),
            "tool '{name}' path is not a regular file"
        );
        download::validate_sha256(&entry.sha256)?;
        if let Some(archive) = &entry.archive {
            ensure!(
                archive.is_absolute(),
                "tool '{name}' archive path is not absolute"
            );
            ensure!(archive.is_file(), "tool '{name}' archive path is missing");
        }
    }
    for variable in ["PATH", "PSModulePath"] {
        if let Some(value) = report.environment.get(variable) {
            for directory in env::split_paths(value) {
                ensure!(
                    directory.is_absolute(),
                    "toolchain {variable} contains a relative path"
                );
                ensure!(
                    directory.is_dir(),
                    "toolchain {variable} contains a missing directory"
                );
            }
        }
    }
    Ok(Toolset { report })
}

pub(crate) fn write_json_report<T: Serialize>(path: &Path, report: &T) -> Result<()> {
    use std::io::Write;
    use tempfile::NamedTempFile;

    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(parent).context("could not create toolchain report directory")?;
    let parent = dunce::canonicalize(parent).context("could not resolve report directory")?;
    let name = path
        .file_name()
        .context("toolchain report path has no file name")?;
    let destination = parent.join(name);
    if let Ok(metadata) = fs::symlink_metadata(&destination) {
        ensure!(
            !metadata.file_type().is_symlink() && metadata.is_file(),
            "existing report destination must be a regular file"
        );
    }
    let mut temporary =
        NamedTempFile::new_in(&parent).context("could not create temporary toolchain report")?;
    serde_json::to_writer_pretty(&mut temporary, report)
        .context("could not encode toolchain report")?;
    temporary
        .write_all(b"\n")
        .context("could not finish toolchain report")?;
    temporary
        .flush()
        .context("could not flush toolchain report")?;
    temporary
        .persist(&destination)
        .context("could not publish toolchain report")?;
    Ok(())
}
