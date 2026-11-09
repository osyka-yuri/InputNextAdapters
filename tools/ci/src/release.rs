//! Release identity, artifact qualification, and draft authority.

mod artifacts;
mod draft;
mod identity;
mod verify;

use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};
use clap::{Args, Subcommand};

use crate::process::Runner;
use crate::tooling::load_toolset;

/// Restricted release operations exposed by the host CLI.
#[derive(Debug, Subcommand)]
pub enum ReleaseCommand {
    Qualify(QualifyArgs),
    Artifacts(ArtifactsArgs),
    Verify(VerifyArgs),
    Draft(DraftArgs),
}

impl ReleaseCommand {
    /// Runs one release qualification or draft-authority operation.
    ///
    /// # Errors
    ///
    /// Returns an error if any trusted workflow, tag, artifact, report, or package input is invalid.
    pub fn run(self, runner: &mut dyn Runner) -> Result<()> {
        match self {
            Self::Qualify(args) => args.run(runner),
            Self::Artifacts(args) => args.run(runner),
            Self::Verify(args) => verify::run(&args, runner),
            Self::Draft(args) => draft::run(&args, runner),
        }
    }
}

#[derive(Debug, Args)]
pub struct QualifyArgs {
    #[arg(long)]
    root: PathBuf,
    #[arg(long)]
    git: PathBuf,
    #[arg(long)]
    tag: String,
    #[arg(long)]
    out_identity: PathBuf,
    #[arg(long)]
    out_source_data: Option<PathBuf>,
    #[arg(long)]
    github_output: Option<PathBuf>,
}

impl QualifyArgs {
    fn run(self, runner: &mut dyn Runner) -> Result<()> {
        let context = identity::WorkflowContext::from_environment()?;
        let release = identity::qualify(&context, &self.root, &self.git, &self.tag, runner)?;
        validate_output_path(&self.root, &self.out_identity)?;
        ensure_output_absent(&self.out_identity)?;
        if let Some(path) = &self.github_output {
            validate_output_path(&self.root, path)?;
            ensure!(
                normalized_output_target(path)? != normalized_output_target(&self.out_identity)?,
                "identity and GitHub outputs must use different paths"
            );
        }
        if let Some(path) = &self.out_source_data {
            let identity_target = normalized_output_target(&self.out_identity)?;
            let source_target = normalized_output_target(path)?;
            ensure!(
                identity_target != source_target,
                "identity and tag source outputs must use different paths"
            );
            if let Some(github_output) = &self.github_output {
                ensure!(
                    normalized_output_target(github_output)? != source_target,
                    "GitHub output and tag source outputs must use different paths"
                );
            }
            verify::materialize_tag_source_to(
                &self.root,
                &self.git,
                &release.source_commit,
                path,
                runner,
            )?;
        }
        identity::write_identity(&self.out_identity, &release)?;
        if let Some(path) = &self.github_output {
            identity::append_github_output(path, &release)?;
        }
        println!(
            "qualified {} {} from {}",
            release.product.as_str(),
            release.version,
            release.source_commit
        );
        Ok(())
    }
}

#[derive(Debug, Args)]
pub struct ArtifactsArgs {
    #[arg(long)]
    root: PathBuf,
    #[arg(long)]
    git: PathBuf,
    #[arg(long)]
    tag: String,
    #[arg(long)]
    toolset: PathBuf,
    #[arg(long)]
    out_receipt: PathBuf,
    #[arg(long)]
    github_output: Option<PathBuf>,
}

impl ArtifactsArgs {
    fn run(self, runner: &mut dyn Runner) -> Result<()> {
        let context = identity::WorkflowContext::from_environment()?;
        let release = identity::qualify(&context, &self.root, &self.git, &self.tag, runner)?;
        validate_output_path(&self.root, &self.out_receipt)?;
        ensure_output_absent(&self.out_receipt)?;
        if let Some(path) = &self.github_output {
            validate_output_path(&self.root, path)?;
            ensure!(
                normalized_output_target(path)? != normalized_output_target(&self.out_receipt)?,
                "artifact receipt and GitHub outputs must use different paths"
            );
        }
        let toolset = load_toolset(&self.toolset)?;
        let receipt = artifacts::select_current_run(&context, &release, &toolset, runner)?;
        artifacts::write_receipt(&self.out_receipt, &receipt)?;
        if let Some(path) = &self.github_output {
            artifacts::append_artifact_ids(path, &receipt)?;
        }
        println!(
            "qualified {} current-run artifacts for {} {}",
            receipt.artifacts.len(),
            release.product.as_str(),
            release.version
        );
        Ok(())
    }
}

#[derive(Debug, Args)]
pub struct VerifyArgs {
    #[arg(long)]
    pub(super) root: PathBuf,
    #[arg(long)]
    pub(super) git: PathBuf,
    #[arg(long)]
    pub(super) tag: String,
    #[arg(long)]
    pub(super) toolset: PathBuf,
    #[arg(long)]
    pub(super) receipt: PathBuf,
    #[arg(long)]
    pub(super) handoff: PathBuf,
    #[arg(long)]
    pub(super) runtime_manifest: PathBuf,
    #[arg(long)]
    pub(super) report: Option<PathBuf>,
}

#[derive(Debug, Args)]
pub struct DraftArgs {
    #[arg(long)]
    pub(super) root: PathBuf,
    #[arg(long)]
    pub(super) git: PathBuf,
    #[arg(long)]
    pub(super) tag: String,
    #[arg(long)]
    pub(super) toolset: PathBuf,
    #[arg(long)]
    pub(super) receipt: PathBuf,
    #[arg(long)]
    pub(super) handoff: PathBuf,
    #[arg(long)]
    pub(super) runtime_manifest: PathBuf,
    #[arg(long)]
    pub(super) report: Option<PathBuf>,
}

fn validate_output_path(root: &Path, path: &Path) -> Result<()> {
    ensure!(path.is_absolute(), "release output path must be absolute");
    let root = dunce::canonicalize(root).context("resolve trusted workflow checkout")?;
    let parent = path.parent().context("release output path has no parent")?;
    let parent = dunce::canonicalize(parent).context("resolve release output directory")?;
    ensure!(
        !parent.starts_with(&root),
        "release output must be outside the trusted source checkout"
    );
    if let Ok(metadata) = std::fs::symlink_metadata(path) {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "release output destination must be a regular file"
        );
    }
    Ok(())
}

fn normalized_output_target(path: &Path) -> Result<PathBuf> {
    ensure!(path.is_absolute(), "release output path must be absolute");
    let parent = path.parent().context("release output path has no parent")?;
    let parent = dunce::canonicalize(parent).context("resolve release output directory")?;
    let name = path
        .file_name()
        .context("release output path has no name")?;
    Ok(parent.join(name))
}

fn ensure_output_absent(path: &Path) -> Result<()> {
    match std::fs::symlink_metadata(path) {
        Ok(_) => anyhow::bail!("release output destination already exists"),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error).context("could not inspect release output destination"),
    }
}
