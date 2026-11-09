//! Safe local materialization and bounded reads for release handoffs.

use std::collections::{BTreeMap, BTreeSet};
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use tempfile::TempDir;

use crate::process::{CommandSpec, Runner, run_checked};
use crate::tooling::{ArchiveFormat, extract_archive, inspect_archive};

pub(super) const MAX_TAG_ARCHIVE_BYTES: u64 = 2 * 1024 * 1024 * 1024;
pub(in crate::release) const MAX_REPORT_BYTES: u64 = 16 * 1024 * 1024;
pub(super) const MAX_RUNTIME_MANIFEST_BYTES: u64 = 1024 * 1024;

/// A safely extracted, data-only Git tree for a previously qualified tag.
#[derive(Debug)]
pub(super) struct TagSourceTree {
    _temporary: TempDir,
    root: PathBuf,
}

impl TagSourceTree {
    pub(super) fn root(&self) -> &Path {
        &self.root
    }
}

pub(super) fn materialize_tag_source(
    workflow_root: &Path,
    git: &Path,
    source_commit: &str,
    runner: &mut dyn Runner,
) -> Result<TagSourceTree> {
    materialize_tag_source_in(workflow_root, git, source_commit, None, runner)
}

pub(super) fn materialize_tag_source_to(
    workflow_root: &Path,
    git: &Path,
    source_commit: &str,
    output: &Path,
    runner: &mut dyn Runner,
) -> Result<PathBuf> {
    ensure!(
        output.is_absolute(),
        "tag source output path must be absolute"
    );
    let workflow_root =
        dunce::canonicalize(workflow_root).context("resolve trusted workflow checkout")?;
    let parent = output
        .parent()
        .context("tag source output path has no parent")?;
    let parent = dunce::canonicalize(parent).context("resolve tag source output directory")?;
    let name = output
        .file_name()
        .context("tag source output path has no name")?;
    let destination = parent.join(name);
    ensure!(
        !destination.starts_with(&workflow_root),
        "tag source output must be outside the trusted source checkout"
    );
    match std::fs::symlink_metadata(&destination) {
        Ok(_) => anyhow::bail!("tag source output destination already exists"),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(error) => return Err(error).context("could not inspect tag source output destination"),
    }

    let source =
        materialize_tag_source_in(&workflow_root, git, source_commit, Some(&parent), runner)?;
    std::fs::rename(&source.root, &destination)
        .context("could not publish audited tag source data")?;
    Ok(destination)
}

fn materialize_tag_source_in(
    workflow_root: &Path,
    git: &Path,
    source_commit: &str,
    temporary_parent: Option<&Path>,
    runner: &mut dyn Runner,
) -> Result<TagSourceTree> {
    ensure!(
        workflow_root.is_absolute(),
        "trusted workflow checkout path must be absolute"
    );
    ensure!(git.is_absolute(), "Git executable path must be absolute");
    ensure!(
        source_commit.len() == 40
            && source_commit
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)),
        "tag source must be a full lowercase Git commit SHA"
    );
    let workflow_root =
        dunce::canonicalize(workflow_root).context("resolve trusted workflow checkout")?;
    let git = dunce::canonicalize(git).context("resolve trusted Git executable")?;
    ensure!(git.is_file(), "Git executable is missing");

    let temporary = match temporary_parent {
        Some(parent) => tempfile::Builder::new()
            .prefix("inputnext-tag-source-")
            .tempdir_in(parent)
            .context("create tag source workspace")?,
        None => tempfile::tempdir().context("create tag source workspace")?,
    };
    let archive = temporary.path().join("tag-source.zip");
    let output = run_checked(
        runner,
        &CommandSpec {
            program: git,
            args: [
                OsString::from("-c"),
                OsString::from("core.autocrlf=false"),
                OsString::from("-c"),
                OsString::from("core.eol=lf"),
                OsString::from("archive"),
                OsString::from("--format=zip"),
                OsString::from("--prefix=tag-source/"),
                OsString::from("--output"),
                archive.as_os_str().to_owned(),
                OsString::from(source_commit),
            ]
            .into(),
            cwd: workflow_root,
            env: BTreeMap::default(),
            timeout: Duration::from_secs(60),
            output_limit: 1024 * 1024,
        },
    )
    .context("could not create data-only tag archive")?;
    ensure!(
        output.stdout.is_empty(),
        "Git archive unexpectedly wrote data to stdout"
    );

    let metadata =
        std::fs::symlink_metadata(&archive).context("Git did not create the tag source archive")?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "tag source archive must be a regular file"
    );
    ensure!(
        metadata.len() > 0 && metadata.len() <= MAX_TAG_ARCHIVE_BYTES,
        "tag source archive has an invalid size"
    );
    inspect_archive(&archive, ArchiveFormat::Zip)
        .context("tag source archive failed safe inspection")?;

    let extraction = temporary.path().join("extracted");
    extract_archive(&archive, ArchiveFormat::Zip, &extraction)
        .context("could not safely extract tag source data")?;
    let root = extraction.join("tag-source");
    let metadata = std::fs::symlink_metadata(&root)
        .context("tag source archive lacks its expected top-level directory")?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "tag source root must be a real directory"
    );
    let root = dunce::canonicalize(root).context("resolve extracted tag source")?;
    Ok(TagSourceTree {
        _temporary: temporary,
        root,
    })
}

pub(super) fn resolve_handoff_root(path: &Path) -> Result<PathBuf> {
    ensure!(path.is_absolute(), "handoff root must be absolute");
    let metadata = std::fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect handoff root {}", path.display()))?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "handoff root must be a real directory"
    );
    dunce::canonicalize(path).context("resolve handoff root")
}

pub(super) fn validate_handoff_directories(
    handoff: &Path,
    expected_names: &BTreeSet<String>,
) -> Result<PathBuf> {
    let handoff = resolve_handoff_root(handoff)?;
    let mut actual = BTreeSet::new();
    for entry in std::fs::read_dir(&handoff).context("could not list downloaded handoff root")? {
        let entry = entry.context("could not read downloaded handoff entry")?;
        let name = entry
            .file_name()
            .into_string()
            .map_err(|_| anyhow::anyhow!("handoff directory name is not valid Unicode"))?;
        let metadata = std::fs::symlink_metadata(entry.path())
            .context("could not inspect handoff directory")?;
        ensure!(
            metadata.is_dir() && !metadata.file_type().is_symlink(),
            "handoff root must contain only artifact directories"
        );
        ensure!(
            actual.insert(name),
            "handoff contains duplicate artifact directory names"
        );
    }
    ensure!(
        &actual == expected_names,
        "handoff artifact directory set differs from the current-run receipt"
    );
    Ok(handoff)
}

pub(super) fn inspect_handoff_row(
    handoff: &Path,
    artifact_name: &str,
    expected_files: &BTreeSet<String>,
) -> Result<BTreeMap<String, PathBuf>> {
    let handoff = resolve_handoff_root(handoff)?;
    ensure!(
        artifact_name.starts_with("release-")
            && artifact_name.bytes().all(|byte| byte.is_ascii_lowercase()
                || byte.is_ascii_digit()
                || b"-_.".contains(&byte)),
        "handoff artifact name is invalid"
    );
    let directory = handoff.join(artifact_name);
    let metadata = std::fs::symlink_metadata(&directory)
        .with_context(|| format!("missing downloaded artifact directory '{artifact_name}'"))?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "artifact handoff entry must be a real directory"
    );
    let directory = dunce::canonicalize(directory).context("resolve artifact handoff directory")?;
    ensure!(
        directory.starts_with(&handoff),
        "artifact handoff directory escaped its root"
    );

    let mut files = BTreeMap::new();
    let mut folded = BTreeSet::new();
    for entry in
        std::fs::read_dir(&directory).context("could not list artifact handoff directory")?
    {
        let entry = entry.context("could not read artifact handoff entry")?;
        let path = entry.path();
        let name = entry
            .file_name()
            .into_string()
            .map_err(|_| anyhow::anyhow!("artifact file name is not valid Unicode"))?;
        let metadata =
            std::fs::symlink_metadata(&path).context("could not inspect artifact handoff file")?;
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "artifact handoff must contain only regular files"
        );
        ensure!(
            folded.insert(name.to_ascii_lowercase()),
            "artifact handoff contains case-colliding files"
        );
        ensure!(
            path.starts_with(&directory),
            "artifact file escaped its handoff directory"
        );
        ensure!(
            files.insert(name.clone(), path).is_none(),
            "artifact handoff contains a duplicate file"
        );
    }
    ensure!(
        &files.keys().cloned().collect::<BTreeSet<_>>() == expected_files,
        "artifact handoff file set does not match the expected reports and packages"
    );
    Ok(files)
}

pub(in crate::release) fn read_bounded_regular_file(
    path: &Path,
    maximum: u64,
    label: &str,
) -> Result<Vec<u8>> {
    let metadata = std::fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect {label} {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "{label} must be a regular file"
    );
    ensure!(metadata.len() <= maximum, "{label} exceeds the size limit");
    std::fs::read(path).with_context(|| format!("could not read {label} {}", path.display()))
}

pub(super) fn read_runtime_manifest(path: &Path) -> Result<Vec<u8>> {
    read_bounded_regular_file(
        path,
        MAX_RUNTIME_MANIFEST_BYTES,
        "published runtime manifest",
    )
}
