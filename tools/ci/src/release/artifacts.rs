//! Current-run GitHub Actions artifact selection and receipt verification.

use std::collections::{BTreeMap, BTreeSet};
use std::ffi::OsString;
use std::io::Write;
use std::path::Path;
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use time::OffsetDateTime;
use time::format_description::well_known::Rfc3339;

use crate::model::Check;
use crate::process::{CommandSpec, Runner, run_checked};
use crate::tooling::{Toolset, write_json_report};

use self::pagination::fetch_all_pages;
use super::identity::{ReleaseIdentity, WorkflowContext, required_rows};

mod pagination;

const GH_OUTPUT_LIMIT: usize = 16 * 1024 * 1024;

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct ArtifactReceipt {
    pub schema_version: u32,
    pub repository: String,
    pub run_id: String,
    pub run_attempt: String,
    pub workflow_commit: String,
    pub product: String,
    pub version: String,
    pub tag: String,
    pub source_commit: String,
    pub run_started_at: String,
    pub artifacts: Vec<ArtifactRecord>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct ArtifactRecord {
    pub id: u64,
    pub name: String,
    pub digest: String,
    pub created_at: String,
}

pub(super) fn select_current_run(
    context: &WorkflowContext,
    identity: &ReleaseIdentity,
    toolset: &Toolset,
    runner: &mut dyn Runner,
) -> Result<ArtifactReceipt> {
    context.validate()?;
    let gh = toolset.executable("gh")?.to_path_buf();
    let run_id = parse_positive_u64(&context.run_id, "run ID")?;
    let attempt = parse_positive_u64(&context.run_attempt, "run attempt")?;
    let attempt_path = format!(
        "repos/{}/actions/runs/{run_id}/attempts/{attempt}",
        context.repository
    );
    let run = api_json(
        &gh,
        &context.repository,
        &context.run_id,
        &attempt_path,
        toolset,
        runner,
    )?;
    validate_run(&run, context)?;

    let jobs = fetch_all_pages(
        &format!("{attempt_path}/jobs?per_page=100"),
        "jobs",
        |endpoint| {
            api_json(
                &gh,
                &context.repository,
                &context.run_id,
                endpoint,
                toolset,
                runner,
            )
        },
    )?;
    let run_started_at = required_string(&run, "run_started_at")?.to_owned();
    validate_jobs(&jobs, identity.product)?;

    let artifacts = fetch_all_pages(
        &format!(
            "repos/{}/actions/runs/{run_id}/artifacts?per_page=100",
            context.repository
        ),
        "artifacts",
        |endpoint| {
            api_json(
                &gh,
                &context.repository,
                &context.run_id,
                endpoint,
                toolset,
                runner,
            )
        },
    )?;
    let selected = select_expected_artifacts(&artifacts, context, identity, &run_started_at)?;

    Ok(ArtifactReceipt {
        schema_version: 1,
        repository: context.repository.clone(),
        run_id: context.run_id.clone(),
        run_attempt: context.run_attempt.clone(),
        workflow_commit: context.workflow_commit.clone(),
        product: identity.product.as_str().to_owned(),
        version: identity.version.clone(),
        tag: identity.tag.clone(),
        source_commit: identity.source_commit.clone(),
        run_started_at,
        artifacts: selected,
    })
}

pub(super) fn verify_receipt(
    path: &Path,
    context: &WorkflowContext,
    identity: &ReleaseIdentity,
    toolset: &Toolset,
    runner: &mut dyn Runner,
) -> Result<ArtifactReceipt> {
    context.validate()?;
    let receipt = read_receipt(path)?;
    ensure!(
        receipt.repository == context.repository,
        "artifact receipt repository differs from the current run"
    );
    ensure!(
        receipt.run_id == context.run_id,
        "artifact receipt run ID differs from the current run"
    );
    ensure!(
        receipt.run_attempt == context.run_attempt,
        "artifact receipt run attempt differs from the current attempt"
    );
    ensure!(
        receipt.workflow_commit == context.workflow_commit,
        "artifact receipt workflow commit differs from trusted main"
    );
    ensure!(
        receipt.product == identity.product.as_str(),
        "artifact receipt product differs from the requested tag"
    );
    ensure!(
        receipt.version == identity.version,
        "artifact receipt version differs from the requested tag"
    );
    ensure!(
        receipt.tag == identity.tag,
        "artifact receipt tag differs from the requested tag"
    );
    ensure!(
        receipt.source_commit == identity.source_commit,
        "artifact receipt source commit differs from the qualified tag"
    );

    let current = select_current_run(context, identity, toolset, runner)?;
    ensure!(
        receipt.run_started_at == current.run_started_at,
        "artifact receipt belongs to a different run attempt"
    );
    ensure!(
        receipt.artifacts == current.artifacts,
        "artifact receipt differs from current GitHub artifact service identity"
    );
    Ok(receipt)
}

pub(super) fn validate_remote_tag(
    context: &WorkflowContext,
    identity: &ReleaseIdentity,
    toolset: &Toolset,
    runner: &mut dyn Runner,
) -> Result<()> {
    let gh = toolset.executable("gh")?.to_path_buf();
    let endpoint = format!(
        "repos/{}/commits/{}",
        context.repository,
        encode_path_segment(&identity.tag)
    );
    let commit = api_json(
        &gh,
        &context.repository,
        &context.run_id,
        &endpoint,
        toolset,
        runner,
    )?;
    ensure!(
        required_string(&commit, "sha")? == identity.source_commit,
        "remote product tag no longer resolves to the qualified source commit"
    );
    Ok(())
}

pub(super) fn write_receipt(path: &Path, receipt: &ArtifactReceipt) -> Result<()> {
    ensure!(
        path.is_absolute(),
        "artifact receipt output path must be absolute"
    );
    write_json_report(path, receipt)
}

pub(super) fn append_artifact_ids(path: &Path, receipt: &ArtifactReceipt) -> Result<()> {
    let ids = receipt
        .artifacts
        .iter()
        .map(|artifact| artifact.id.to_string())
        .collect::<Vec<_>>()
        .join(",");
    ensure!(!ids.is_empty(), "artifact receipt contains no artifact IDs");
    ensure!(
        ids.bytes()
            .all(|byte| byte.is_ascii_digit() || byte == b','),
        "artifact ID output contains an unsafe character"
    );
    append_output(path, "artifact_ids", &ids)
}

fn append_output(path: &Path, name: &str, value: &str) -> Result<()> {
    ensure!(path.is_absolute(), "GitHub output path must be absolute");
    ensure!(
        name.bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte == b'_'),
        "GitHub output name is invalid"
    );
    ensure!(
        !value.contains('\r') && !value.contains('\n'),
        "GitHub output contains a line break"
    );
    let metadata = std::fs::symlink_metadata(path);
    if let Ok(metadata) = metadata {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "GitHub output must be a regular file"
        );
    }
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .with_context(|| format!("could not open GitHub output {}", path.display()))?;
    writeln!(file, "{name}={value}").context("could not append GitHub output")?;
    file.flush().context("could not flush GitHub output")
}

fn read_receipt(path: &Path) -> Result<ArtifactReceipt> {
    let metadata = std::fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect artifact receipt {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "artifact receipt must be a regular file"
    );
    ensure!(
        metadata.len() <= 4 * 1024 * 1024,
        "artifact receipt exceeds the size limit"
    );
    let bytes = std::fs::read(path).context("could not read artifact receipt")?;
    let receipt: ArtifactReceipt =
        crate::json::parse(&bytes).context("artifact receipt is not valid strict JSON")?;
    ensure!(
        receipt.schema_version == 1,
        "unsupported artifact receipt schema"
    );
    Ok(receipt)
}

fn api_json(
    gh: &Path,
    repository: &str,
    run_id: &str,
    endpoint: &str,
    toolset: &Toolset,
    runner: &mut dyn Runner,
) -> Result<Value> {
    ensure!(
        endpoint.starts_with(&format!("repos/{repository}/actions/runs/{run_id}"))
            || endpoint.starts_with(&format!("repos/{repository}/commits/")),
        "GitHub API endpoint is outside the current release identity"
    );
    let environment = toolset
        .environment()
        .iter()
        .map(|(key, value)| (OsString::from(key), OsString::from(value)))
        .chain([
            (OsString::from("GH_PAGER"), OsString::from("cat")),
            (OsString::from("GH_PROMPT_DISABLED"), OsString::from("1")),
            (OsString::from("GH_NO_UPDATE_NOTIFIER"), OsString::from("1")),
        ])
        .collect::<BTreeMap<_, _>>();
    let output = run_checked(
        runner,
        &CommandSpec {
            program: gh.to_path_buf(),
            args: [OsString::from("api"), OsString::from(endpoint)].into(),
            cwd: std::env::current_dir().context("could not read current directory")?,
            env: environment,
            timeout: Duration::from_secs(60),
            output_limit: GH_OUTPUT_LIMIT,
        },
    )
    .context("GitHub API read failed")?;
    crate::json::parse_slice(&output.stdout)
        .context("GitHub API returned invalid or duplicate-key JSON")
}

fn validate_run(run: &Value, context: &WorkflowContext) -> Result<()> {
    ensure!(
        required_u64(run, "id")? == parse_positive_u64(&context.run_id, "run ID")?,
        "GitHub returned a different workflow run ID"
    );
    ensure!(
        required_u64(run, "run_attempt")?
            == parse_positive_u64(&context.run_attempt, "run attempt")?,
        "GitHub returned a different workflow run attempt"
    );
    ensure!(
        required_string(run, "event")? == "workflow_dispatch",
        "GitHub workflow run was not manually dispatched"
    );
    ensure!(
        required_string(run, "head_branch")? == "main",
        "GitHub workflow run head is not main"
    );
    let workflow_path = required_string(run, "path")?;
    ensure!(
        workflow_path == ".github/workflows/release.yml"
            || workflow_path == ".github/workflows/release.yml@main"
            || workflow_path == ".github/workflows/release.yml@refs/heads/main",
        "GitHub workflow run is not the trusted release workflow"
    );
    ensure!(
        required_string(run, "head_sha")? == context.workflow_commit,
        "GitHub workflow run commit differs from trusted workflow source"
    );
    parse_timestamp(
        required_string(run, "run_started_at")?,
        "workflow run start time",
    )?;
    Ok(())
}

fn validate_jobs(jobs: &Value, product: crate::model::Product) -> Result<()> {
    let entries = jobs
        .get("jobs")
        .and_then(Value::as_array)
        .context("GitHub jobs response has no jobs array")?;
    ensure!(
        required_u64(jobs, "total_count")? == u64::try_from(entries.len())?,
        "GitHub job response is incomplete or paginated"
    );
    let expected = required_rows(product)
        .into_iter()
        .map(|(product, architecture)| format!("Package and qualify ({product}, {architecture})"))
        .collect::<Vec<_>>();
    for name in expected {
        let matches = entries
            .iter()
            .filter(|job| job.get("name").and_then(Value::as_str) == Some(&name))
            .collect::<Vec<_>>();
        ensure!(
            matches.len() == 1,
            "expected exactly one current-attempt job named '{name}'"
        );
        let job = matches[0];
        ensure!(
            required_string(job, "status")? == "completed",
            "required matrix job '{name}' is incomplete"
        );
        ensure!(
            required_string(job, "conclusion")? == "success",
            "required matrix job '{name}' did not succeed"
        );
    }
    Ok(())
}

fn select_expected_artifacts(
    response: &Value,
    context: &WorkflowContext,
    identity: &ReleaseIdentity,
    run_started_at: &str,
) -> Result<Vec<ArtifactRecord>> {
    let entries = response
        .get("artifacts")
        .and_then(Value::as_array)
        .context("GitHub artifacts response has no artifacts array")?;
    let total = required_u64(response, "total_count")?;
    ensure!(
        usize::try_from(total).ok() == Some(entries.len()),
        "GitHub artifact response is incomplete or paginated"
    );
    ensure!(
        entries.len() >= required_rows(identity.product).len(),
        "GitHub current run is missing expected handoff artifacts"
    );
    let started = parse_timestamp(run_started_at, "workflow run start time")?;
    let mut selected = Vec::new();
    let mut ids = BTreeSet::new();
    let expected_names = required_rows(identity.product)
        .into_iter()
        .map(|(product, architecture)| {
            format!(
                "release-{product}-{architecture}-{}-{}",
                context.run_id, context.run_attempt
            )
        })
        .collect::<BTreeSet<_>>();
    let diagnostic_names = required_rows(identity.product)
        .into_iter()
        .map(|(product, architecture)| {
            format!(
                "release-diagnostics-{product}-{architecture}-{}-{}",
                context.run_id, context.run_attempt
            )
        })
        .collect::<BTreeSet<_>>();
    let row_names = required_rows(identity.product)
        .into_iter()
        .map(|(product, architecture)| format!("{product}-{architecture}"))
        .collect::<BTreeSet<_>>();
    for entry in entries {
        let name = required_string(entry, "name")?;
        if expected_names.contains(name) {
            continue;
        }
        if is_prior_attempt_artifact(name, &context.run_id, &context.run_attempt, &row_names)? {
            continue;
        }
        if let Some(attempt) =
            quality_diagnostic_attempt(name, &context.run_id, &context.run_attempt)?
        {
            if attempt == parse_positive_u64(&context.run_attempt, "run attempt")? {
                validate_diagnostic_artifact(entry, name, context, &started, &mut ids)?;
            } else {
                validate_artifact_run(entry, name, context, "quality diagnostic artifact")?;
            }
            continue;
        }
        ensure!(
            diagnostic_names.contains(name),
            "GitHub current run contains an unexpected artifact '{name}'"
        );
        validate_diagnostic_artifact(entry, name, context, &started, &mut ids)?;
    }
    for (product, architecture) in required_rows(identity.product) {
        let name = format!(
            "release-{product}-{architecture}-{}-{}",
            context.run_id, context.run_attempt
        );
        let matches = entries
            .iter()
            .filter(|entry| entry.get("name").and_then(Value::as_str) == Some(&name))
            .collect::<Vec<_>>();
        ensure!(
            matches.len() == 1,
            "expected exactly one current artifact named '{name}'"
        );
        selected.push(validate_current_artifact(
            matches[0], &name, context, &started, &mut ids,
        )?);
    }
    Ok(selected)
}

fn quality_diagnostic_attempt(
    name: &str,
    run_id: &str,
    current_attempt: &str,
) -> Result<Option<u64>> {
    let current_attempt = parse_positive_u64(current_attempt, "run attempt")?;
    for key in crate::plan::supported_keys()
        .into_iter()
        .filter(|key| key.check != Check::NativeTests)
    {
        let prefix = format!(
            "check-{}-{}-{}-{run_id}-",
            key.product.as_str(),
            key.architecture.as_str(),
            key.check.as_str()
        );
        let Some(attempt) = name.strip_prefix(&prefix) else {
            continue;
        };
        let attempt = parse_positive_u64(attempt, "quality diagnostic run attempt")?;
        ensure!(
            attempt <= current_attempt,
            "quality diagnostic artifact '{name}' belongs to a future run attempt"
        );
        return Ok(Some(attempt));
    }
    Ok(None)
}

fn validate_diagnostic_artifact(
    artifact: &Value,
    name: &str,
    context: &WorkflowContext,
    started: &OffsetDateTime,
    ids: &mut BTreeSet<u64>,
) -> Result<()> {
    ensure!(
        artifact.get("expired").and_then(Value::as_bool) == Some(false),
        "diagnostic artifact '{name}' is expired"
    );
    let id = required_u64(artifact, "id")?;
    ensure!(
        id > 0 && ids.insert(id),
        "GitHub returned a duplicate or invalid artifact ID"
    );
    validate_artifact_digest(required_string(artifact, "digest")?)?;
    ensure!(
        parse_timestamp(
            required_string(artifact, "created_at")?,
            "artifact creation time"
        )? >= *started,
        "diagnostic artifact '{name}' predates the current run attempt"
    );
    validate_artifact_run(artifact, name, context, "diagnostic artifact")
}

fn validate_current_artifact(
    artifact: &Value,
    name: &str,
    context: &WorkflowContext,
    started: &OffsetDateTime,
    ids: &mut BTreeSet<u64>,
) -> Result<ArtifactRecord> {
    ensure!(
        artifact.get("expired").and_then(Value::as_bool) == Some(false),
        "artifact '{name}' is expired"
    );
    let id = required_u64(artifact, "id")?;
    ensure!(
        id > 0 && ids.insert(id),
        "GitHub returned a duplicate or invalid artifact ID"
    );
    ensure!(
        required_u64(artifact, "size_in_bytes")? > 0,
        "artifact '{name}' is empty"
    );
    let digest = required_string(artifact, "digest")?.to_owned();
    validate_artifact_digest(&digest)?;
    let created_at = required_string(artifact, "created_at")?.to_owned();
    ensure!(
        parse_timestamp(&created_at, "artifact creation time")? >= *started,
        "artifact '{name}' predates the current run attempt"
    );
    validate_artifact_run(artifact, name, context, "artifact")?;
    Ok(ArtifactRecord {
        id,
        name: name.to_owned(),
        digest,
        created_at,
    })
}

fn validate_artifact_run(
    artifact: &Value,
    name: &str,
    context: &WorkflowContext,
    label: &str,
) -> Result<()> {
    let artifact_run = artifact
        .get("workflow_run")
        .with_context(|| format!("{label} lacks workflow run identity"))?;
    ensure!(
        required_u64(artifact_run, "id")? == parse_positive_u64(&context.run_id, "run ID")?,
        "{label} '{name}' belongs to another workflow run"
    );
    ensure!(
        required_string(artifact_run, "head_sha")? == context.workflow_commit,
        "{label} '{name}' workflow commit differs from trusted main"
    );
    ensure!(
        required_string(artifact_run, "head_branch")? == "main",
        "{label} '{name}' was not produced by the trusted main workflow"
    );
    Ok(())
}

fn required_string<'a>(value: &'a Value, key: &str) -> Result<&'a str> {
    value
        .get(key)
        .and_then(Value::as_str)
        .with_context(|| format!("GitHub response field '{key}' is missing or not a string"))
}

fn required_u64(value: &Value, key: &str) -> Result<u64> {
    value.get(key).and_then(Value::as_u64).with_context(|| {
        format!("GitHub response field '{key}' is missing or not an unsigned integer")
    })
}

fn parse_positive_u64(value: &str, label: &str) -> Result<u64> {
    let parsed = value
        .parse::<u64>()
        .with_context(|| format!("invalid {label}"))?;
    ensure!(parsed > 0 && parsed.to_string() == value, "invalid {label}");
    Ok(parsed)
}

fn parse_timestamp(value: &str, label: &str) -> Result<OffsetDateTime> {
    OffsetDateTime::parse(value, &Rfc3339).with_context(|| format!("invalid {label}"))
}

fn validate_artifact_digest(value: &str) -> Result<()> {
    let digest = value
        .strip_prefix("sha256:")
        .context("artifact digest is not a SHA256 digest")?;
    ensure!(
        digest.len() == 64
            && digest
                .bytes()
                .all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()),
        "artifact digest is not lowercase SHA256"
    );
    Ok(())
}

fn is_prior_attempt_artifact(
    name: &str,
    run_id: &str,
    current_attempt: &str,
    rows: &BTreeSet<String>,
) -> Result<bool> {
    let current_attempt = parse_positive_u64(current_attempt, "run attempt")?;
    for prefix in ["release-", "release-diagnostics-"] {
        for row in rows {
            let marker = format!("{prefix}{row}-{run_id}-");
            let Some(attempt) = name.strip_prefix(&marker) else {
                continue;
            };
            let parsed = parse_positive_u64(attempt, "artifact run attempt")?;
            return Ok(parsed < current_attempt);
        }
    }
    Ok(false)
}

fn encode_path_segment(value: &str) -> String {
    value.replace('%', "%25").replace('/', "%2F")
}
#[cfg(test)]
mod tests;
