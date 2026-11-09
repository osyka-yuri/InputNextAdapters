use std::collections::BTreeMap;
use std::env;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};

use crate::model::Product;
use crate::process::{CommandSpec, Runner, run_checked};
use crate::source::parse_project_version;
use crate::tooling::write_json_report;

/// Authority supplied by the current workflow rather than a build artifact.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct WorkflowContext {
    pub repository: String,
    pub event: String,
    pub source_ref: String,
    pub source_commit: String,
    pub workflow_commit: String,
    pub workflow_repository: String,
    pub workflow_ref: String,
    pub workflow_path: String,
    pub run_id: String,
    pub run_attempt: String,
}

impl WorkflowContext {
    pub(super) fn from_environment() -> Result<Self> {
        fn value(name: &str) -> Result<String> {
            env::var(name).with_context(|| format!("required workflow context {name} is missing"))
        }
        Ok(Self {
            repository: value("GITHUB_REPOSITORY")?,
            event: value("GITHUB_EVENT_NAME")?,
            source_ref: value("GITHUB_REF")?,
            source_commit: value("GITHUB_SHA")?,
            workflow_commit: value("ADAPTERS_WORKFLOW_SHA")?,
            workflow_repository: value("ADAPTERS_WORKFLOW_REPOSITORY")?,
            workflow_ref: value("ADAPTERS_WORKFLOW_REF")?,
            workflow_path: value("ADAPTERS_WORKFLOW_PATH")?,
            run_id: value("GITHUB_RUN_ID")?,
            run_attempt: value("GITHUB_RUN_ATTEMPT")?,
        })
    }

    pub(super) fn validate(&self) -> Result<()> {
        validate_repository(&self.repository)?;
        ensure!(
            self.event == "workflow_dispatch",
            "release requires a manual dispatch"
        );
        ensure!(
            self.source_ref == "refs/heads/main",
            "release workflow must run from main"
        );
        validate_commit(&self.source_commit)?;
        validate_commit(&self.workflow_commit)?;
        ensure!(
            self.source_commit == self.workflow_commit,
            "workflow source SHA differs from the dispatch"
        );
        ensure!(
            self.repository
                .eq_ignore_ascii_case(&self.workflow_repository),
            "workflow repository differs from the dispatch"
        );
        ensure!(
            self.workflow_path == ".github/workflows/release.yml",
            "unexpected release workflow path"
        );
        ensure!(
            self.workflow_ref
                == format!(
                    "{}/{}@refs/heads/main",
                    self.workflow_repository, self.workflow_path
                ),
            "release workflow ref is not trusted main"
        );
        for (label, value) in [("run ID", &self.run_id), ("run attempt", &self.run_attempt)] {
            let parsed = value
                .parse::<u64>()
                .with_context(|| format!("invalid {label}"))?;
            ensure!(
                parsed > 0 && parsed.to_string() == *value,
                "invalid {label}"
            );
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct ReleaseIdentity {
    pub schema_version: u32,
    pub repository: String,
    pub product: Product,
    pub version: String,
    pub tag: String,
    pub source_commit: String,
    pub workflow_commit: String,
    pub run_id: String,
    pub run_attempt: String,
    pub notes: String,
}

pub(super) fn qualify(
    context: &WorkflowContext,
    root: &Path,
    git: &Path,
    tag: &str,
    runner: &mut dyn Runner,
) -> Result<ReleaseIdentity> {
    context.validate()?;
    let (product, version) = parse_tag(tag)?;
    ensure!(
        root.is_absolute(),
        "trusted workflow checkout path must be absolute"
    );
    ensure!(git.is_absolute(), "Git executable path must be absolute");
    let root = dunce::canonicalize(root).context("resolve trusted workflow checkout")?;
    let git = dunce::canonicalize(git).context("resolve trusted Git executable")?;
    ensure!(git.is_file(), "Git executable is missing");
    ensure!(
        git_text(&git, &root, &["rev-parse", "HEAD"], runner)?.trim() == context.workflow_commit,
        "checkout does not match the defining workflow"
    );
    ensure!(
        git_text(
            &git,
            &root,
            &["status", "--porcelain=v1", "--untracked-files=all"],
            runner
        )?
        .trim()
        .is_empty(),
        "trusted workflow checkout is modified"
    );
    git_text(
        &git,
        &root,
        &[
            "merge-base",
            "--is-ancestor",
            &context.workflow_commit,
            "refs/remotes/origin/main",
        ],
        runner,
    )?;
    let tag_ref = format!("refs/tags/{tag}^{{commit}}");
    let commit = git_text(
        &git,
        &root,
        &["rev-parse", "--verify", "--end-of-options", &tag_ref],
        runner,
    )?
    .trim()
    .to_owned();
    validate_commit(&commit)?;
    git_text(
        &git,
        &root,
        &[
            "merge-base",
            "--is-ancestor",
            &commit,
            &context.workflow_commit,
        ],
        runner,
    )?;
    let project = relative_project(product);
    let cmake = git_text(
        &git,
        &root,
        &["show", &format!("{commit}:{project}CMakeLists.txt")],
        runner,
    )?;
    let actual = parse_project_version(product, &cmake)?;
    ensure!(
        actual.to_string() == version,
        "tag version {version} differs from the product CMake version {actual}"
    );
    let changelog = git_text(
        &git,
        &root,
        &["show", &format!("{commit}:{project}CHANGELOG.md")],
        runner,
    )?;
    let notes = version_notes(&changelog, &version)?;
    Ok(ReleaseIdentity {
        schema_version: 1,
        repository: context.repository.clone(),
        product,
        version,
        tag: tag.to_owned(),
        source_commit: commit,
        workflow_commit: context.workflow_commit.clone(),
        run_id: context.run_id.clone(),
        run_attempt: context.run_attempt.clone(),
        notes,
    })
}

pub(super) fn parse_tag(tag: &str) -> Result<(Product, String)> {
    let (name, version) = tag
        .split_once("/v")
        .context("expected a product/vX.Y.Z tag")?;
    let product = match name {
        "foundation" => Product::Foundation,
        "skyrim-se" => Product::SkyrimSe,
        "skyrim-le" => Product::SkyrimLe,
        "fallout4" => Product::Fallout4,
        _ => anyhow::bail!("unknown release product {name}"),
    };
    let parsed = semver::Version::parse(version).context("invalid release version")?;
    ensure!(
        parsed.pre.is_empty() && parsed.build.is_empty() && parsed.to_string() == version,
        "release requires a canonical stable version"
    );
    Ok((product, version.to_owned()))
}

pub(super) fn validate_repository(value: &str) -> Result<()> {
    let parts = value.split('/').collect::<Vec<_>>();
    ensure!(
        parts.len() == 2
            && parts.iter().all(|part| !part.is_empty()
                && *part != "."
                && *part != ".."
                && part.len() <= 100
                && part
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || b"_.-".contains(&byte))),
        "repository must be owner/name"
    );
    Ok(())
}

fn validate_commit(value: &str) -> Result<()> {
    ensure!(
        value.len() == 40
            && value
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)),
        "expected a full lowercase Git commit SHA"
    );
    Ok(())
}

fn relative_project(product: Product) -> &'static str {
    match product {
        Product::Foundation => "",
        Product::SkyrimSe => "adapters/skyrim_se/",
        Product::SkyrimLe => "adapters/skyrim_le/",
        Product::Fallout4 => "adapters/fallout4/",
    }
}

fn version_notes(changelog: &str, version: &str) -> Result<String> {
    let header = format!("## [{version}]");
    let mut found = 0_u8;
    let mut in_section = false;
    let mut notes = Vec::new();
    for line in changelog.lines() {
        if line.starts_with("## ") {
            in_section = line.trim() == header;
            if in_section {
                found = found.saturating_add(1);
            }
        } else if in_section {
            notes.push(line);
        }
    }
    let notes = notes.join("\n").trim().to_owned();
    ensure!(
        found == 1,
        "curated release notes must have exactly one section for {version}"
    );
    ensure!(
        !notes.is_empty(),
        "curated release notes are empty for {version}"
    );
    Ok(notes)
}

pub(super) fn write_identity(path: &Path, identity: &ReleaseIdentity) -> Result<()> {
    ensure!(
        path.is_absolute(),
        "release identity output path must be absolute"
    );
    write_json_report(path, identity)
}

pub(super) fn append_github_output(path: &Path, identity: &ReleaseIdentity) -> Result<()> {
    ensure!(path.is_absolute(), "GitHub output path must be absolute");
    let matrix = required_matrix(identity.product);
    let matrix = serde_json::to_string(&matrix).context("encode release matrix")?;
    let values = [
        ("product", identity.product.as_str().to_owned()),
        ("version", identity.version.clone()),
        ("source_commit", identity.source_commit.clone()),
        ("matrix", matrix),
    ];
    let mut output = String::new();
    for (key, value) in values {
        ensure!(
            !value.contains('\r') && !value.contains('\n'),
            "release output '{key}' contains a line break"
        );
        output.push_str(key);
        output.push('=');
        output.push_str(&value);
        output.push('\n');
    }

    let metadata = fs::symlink_metadata(path);
    if let Ok(metadata) = metadata {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "GitHub output must be a regular file"
        );
    }
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .with_context(|| format!("could not open GitHub output {}", path.display()))?;
    file.write_all(output.as_bytes())
        .with_context(|| format!("could not append GitHub output {}", path.display()))?;
    file.flush().context("could not flush GitHub output")?;
    Ok(())
}

pub(super) fn required_rows(product: Product) -> Vec<(&'static str, &'static str)> {
    let mut rows = vec![("foundation", "x64"), ("foundation", "x86")];
    match product {
        Product::Foundation => rows.extend([
            ("skyrim-se", "x64"),
            ("skyrim-le", "x86"),
            ("fallout4", "x64"),
        ]),
        Product::SkyrimSe => rows.push(("skyrim-se", "x64")),
        Product::SkyrimLe => rows.push(("skyrim-le", "x86")),
        Product::Fallout4 => rows.push(("fallout4", "x64")),
    }
    rows
}

pub(super) fn required_matrix(product: Product) -> serde_json::Value {
    serde_json::json!({
        "include": required_rows(product).into_iter().map(|(product, architecture)| {
            serde_json::json!({ "product": product, "architecture": architecture })
        }).collect::<Vec<_>>()
    })
}

fn git_text(git: &Path, root: &Path, args: &[&str], runner: &mut dyn Runner) -> Result<String> {
    let output = run_checked(
        runner,
        &CommandSpec {
            program: git.to_path_buf(),
            args: args.iter().map(std::ffi::OsString::from).collect(),
            cwd: root.to_path_buf(),
            env: BTreeMap::new(),
            timeout: Duration::from_secs(60),
            output_limit: 1024 * 1024,
        },
    )?;
    String::from_utf8(output.stdout).context("Git returned non-UTF-8 release data")
}
#[cfg(test)]
mod tests;
