//! Explicit draft creation and non-clobbering package upload.

use std::collections::BTreeMap;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use serde::Serialize;
use serde_json::Value;
use tempfile::tempdir;

use crate::process::{CommandSpec, Runner, run_checked};
use crate::tooling::{load_toolset, write_json_report};

use super::identity::ReleaseIdentity;
use super::verify::{self, ReleaseReport, VerifiedAsset};

const GH_OUTPUT_LIMIT: usize = 8 * 1024 * 1024;

pub(super) fn run(args: &super::DraftArgs, runner: &mut dyn Runner) -> Result<()> {
    if let Some(path) = &args.report {
        super::validate_output_path(&args.root, path)?;
    }
    let verified = verify::prepare_release(verify::ReleasePaths::for_draft(args), runner)?;
    let toolset = load_toolset(&args.toolset)?;
    let gh = toolset.executable("gh")?.to_path_buf();
    let environment = github_environment(toolset.environment());
    require_tagged_release_absent(
        runner,
        &gh,
        &environment,
        &args.root,
        &verified.identity.repository,
        &verified.identity.tag,
    )?;
    if let Some(path) = &args.report {
        write_json_report(path, &verified.report("verified"))?;
    }

    let draft = create_draft(
        runner,
        &gh,
        &environment,
        &args.root,
        &verified.identity,
        !verified.assets.is_empty(),
    )?;
    let mut report = verified.report("draft-created");
    report.draft_id = Some(draft.id);
    report.draft_url.clone_from(&draft.html_url);
    write_optional_report(args.report.as_deref(), &report).with_context(|| {
        format!(
            "draft release {} was created (ID {}), but its local status report could not be written",
            verified.identity.tag, draft.id
        )
    })?;

    upload_verified_assets(
        runner,
        &gh,
        &environment,
        &args.root,
        &verified.identity,
        &draft,
        &verified.asset_paths,
        &verified.assets,
        &mut report,
        args.report.as_deref(),
    )?;

    "draft-uploaded".clone_into(&mut report.state);
    report.failure = None;
    write_optional_report(args.report.as_deref(), &report).with_context(|| {
        format!(
            "draft release {} (ID {}) has all assets uploaded, but its final local status report could not be written",
            verified.identity.tag, draft.id
        )
    })?;
    println!(
        "created draft release {} (ID {}) with {} asset(s); it remains unpublished",
        verified.identity.tag,
        draft.id,
        report.uploaded_assets.len()
    );
    Ok(())
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct DraftIdentity {
    id: u64,
    html_url: Option<String>,
}

fn require_tagged_release_absent(
    runner: &mut dyn Runner,
    gh: &Path,
    environment: &BTreeMap<OsString, OsString>,
    root: &Path,
    repository: &str,
    tag: &str,
) -> Result<()> {
    let encoded = encode_path_segment(tag);
    let endpoint = format!("repos/{repository}/releases/tags/{encoded}");
    let output = runner.run(&CommandSpec {
        program: gh.to_path_buf(),
        args: [
            OsString::from("api"),
            OsString::from("--include"),
            OsString::from(endpoint),
        ]
        .into(),
        cwd: dunce::canonicalize(root).context("resolve trusted workflow checkout")?,
        env: environment.clone(),
        timeout: Duration::from_secs(60),
        output_limit: GH_OUTPUT_LIMIT,
    })?;
    match response_status(&output.stdout)? {
        404 => {
            ensure!(
                output.code != Some(0),
                "GitHub returned a successful process status for an absent release"
            );
            Ok(())
        }
        200 => anyhow::bail!(
            "release {tag} already exists; the draft command never edits or replaces releases"
        ),
        status => {
            anyhow::bail!("could not safely check release collision: GitHub returned HTTP {status}")
        }
    }
}

fn create_draft(
    runner: &mut dyn Runner,
    gh: &Path,
    environment: &BTreeMap<OsString, OsString>,
    root: &Path,
    identity: &ReleaseIdentity,
    has_assets: bool,
) -> Result<DraftIdentity> {
    ensure!(
        has_assets,
        "cannot create a release without verified assets"
    );
    let request_directory = tempdir().context("create temporary GitHub request directory")?;
    let request_path = request_directory.path().join("create-draft.json");
    let request = serde_json::json!({
        "tag_name": identity.tag,
        "target_commitish": identity.source_commit,
        "name": format!("InputNextAdapters {} {}", identity.product.as_str(), identity.version),
        "body": identity.notes,
        "draft": true,
        "prerelease": false,
        "make_latest": "false"
    });
    std::fs::write(&request_path, serde_json::to_vec(&request)?)
        .context("write bounded release draft request")?;
    let output = run_checked(
        runner,
        &CommandSpec {
            program: gh.to_path_buf(),
            args: [
                OsString::from("api"),
                OsString::from("--method"),
                OsString::from("POST"),
                OsString::from("--input"),
                request_path.as_os_str().to_owned(),
                OsString::from(format!("repos/{}/releases", identity.repository)),
            ]
            .into(),
            cwd: dunce::canonicalize(root).context("resolve trusted workflow checkout")?,
            env: environment.clone(),
            timeout: Duration::from_secs(60),
            output_limit: GH_OUTPUT_LIMIT,
        },
    )
    .context(
        "draft creation request failed; verify whether GitHub created the draft before retrying",
    )?;
    let release: Value = crate::json::parse_slice(&output.stdout)
        .context("GitHub may have created the draft but returned invalid or duplicate-key JSON")?;
    let id = release
        .get("id")
        .and_then(Value::as_u64)
        .context("GitHub may have created the draft but returned no numeric release ID")?;
    ensure!(id > 0, "created draft ID is invalid");
    ensure!(
        release.get("tag_name").and_then(Value::as_str) == Some(identity.tag.as_str()),
        "created release ID {id} has a tag different from the qualified tag"
    );
    ensure!(
        release.get("draft").and_then(Value::as_bool) == Some(true),
        "created release ID {id} is not a draft"
    );
    ensure!(
        release.get("prerelease").and_then(Value::as_bool) == Some(false),
        "created release ID {id} is unexpectedly a prerelease"
    );
    let html_url = release
        .get("html_url")
        .and_then(Value::as_str)
        .map(str::to_owned);
    Ok(DraftIdentity { id, html_url })
}

fn upload_asset(
    runner: &mut dyn Runner,
    gh: &Path,
    environment: &BTreeMap<OsString, OsString>,
    root: &Path,
    repository: &str,
    tag: &str,
    asset: &Path,
) -> Result<()> {
    ensure!(
        asset.is_absolute(),
        "verified release asset path must be absolute"
    );
    let metadata = std::fs::symlink_metadata(asset).with_context(|| {
        format!(
            "could not inspect verified release asset {}",
            asset.display()
        )
    })?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "verified release asset must be a regular file"
    );
    let file_name = asset
        .file_name()
        .context("verified release asset has no filename")?
        .to_str()
        .context("verified release asset filename is not valid UTF-8")?;
    ensure!(
        file_name.starts_with("InputNextAdapters-")
            && Path::new(file_name)
                .extension()
                .and_then(std::ffi::OsStr::to_str)
                .is_some_and(|extension| extension.eq_ignore_ascii_case("zip")),
        "verified release asset filename is not an expected package ZIP"
    );
    run_checked(
        runner,
        &CommandSpec {
            program: gh.to_path_buf(),
            args: [
                OsString::from("release"),
                OsString::from("upload"),
                OsString::from(tag),
                asset.as_os_str().to_owned(),
                OsString::from("--repo"),
                OsString::from(repository),
            ]
            .into(),
            cwd: dunce::canonicalize(root).context("resolve trusted workflow checkout")?,
            env: environment.clone(),
            timeout: Duration::from_mins(5),
            output_limit: GH_OUTPUT_LIMIT,
        },
    )
    .with_context(|| format!("could not upload verified package asset '{file_name}'"))?;
    Ok(())
}

#[allow(
    clippy::too_many_arguments,
    reason = "upload recovery stays bound to the verified draft"
)]
fn upload_verified_assets(
    runner: &mut dyn Runner,
    gh: &Path,
    environment: &BTreeMap<OsString, OsString>,
    root: &Path,
    identity: &ReleaseIdentity,
    draft: &DraftIdentity,
    asset_paths: &[PathBuf],
    assets: &[VerifiedAsset],
    report: &mut ReleaseReport,
    report_path: Option<&Path>,
) -> Result<()> {
    ensure!(
        !assets.is_empty() && assets.len() == asset_paths.len(),
        "verified asset paths do not match the release inventory"
    );
    for (asset_path, asset) in asset_paths.iter().zip(assets) {
        match upload_asset(
            runner,
            gh,
            environment,
            root,
            &identity.repository,
            &identity.tag,
            asset_path,
        ) {
            Ok(()) => {
                report.uploaded_assets.push(asset.name.clone());
                "draft-uploading".clone_into(&mut report.state);
                write_optional_report(report_path, report).with_context(|| {
                    format!(
                        "draft release {} (ID {}) has uploaded '{}' but its local status report could not be updated",
                        identity.tag, draft.id, asset.name
                    )
                })?;
            }
            Err(error) => {
                "partial-draft".clone_into(&mut report.state);
                report.failure = Some(format!("{error:#}"));
                let report_result = write_optional_report(report_path, report);
                let report_suffix = report_result
                    .err()
                    .map_or_else(String::new, |report_error| {
                        format!("; report update failed: {report_error:#}")
                    });
                anyhow::bail!(
                    "draft release {} was created (ID {}), but upload of '{}' failed; the draft may contain partial assets{}: {error:#}",
                    identity.tag,
                    draft.id,
                    asset.name,
                    report_suffix
                );
            }
        }
    }
    Ok(())
}

fn github_environment(base: &BTreeMap<String, String>) -> BTreeMap<OsString, OsString> {
    let mut environment = base
        .iter()
        .map(|(key, value)| (OsString::from(key), OsString::from(value)))
        .collect::<BTreeMap<_, _>>();
    for (key, value) in [
        ("GH_PAGER", "cat"),
        ("GH_PROMPT_DISABLED", "1"),
        ("GH_NO_UPDATE_NOTIFIER", "1"),
    ] {
        environment.insert(OsString::from(key), OsString::from(value));
    }
    environment
}

fn encode_path_segment(value: &str) -> String {
    value.replace('%', "%25").replace('/', "%2F")
}

fn response_status(bytes: &[u8]) -> Result<u16> {
    let text =
        std::str::from_utf8(bytes).context("GitHub API response headers are not valid UTF-8")?;
    let status_line = text
        .lines()
        .rfind(|line| line.trim_start().starts_with("HTTP/"))
        .context("GitHub API response lacks an HTTP status line")?;
    let status = status_line
        .split_ascii_whitespace()
        .nth(1)
        .context("GitHub API response status is missing")?
        .parse::<u16>()
        .context("GitHub API response status is invalid")?;
    Ok(status)
}

fn write_optional_report(path: Option<&Path>, report: &ReleaseReport) -> Result<()> {
    if let Some(path) = path {
        write_json_report(path, report)?;
    }
    Ok(())
}
#[cfg(test)]
mod tests;
