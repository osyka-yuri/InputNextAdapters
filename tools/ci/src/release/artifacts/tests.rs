use super::{
    encode_path_segment, is_prior_attempt_artifact, select_current_run, validate_jobs, validate_run,
};
use crate::model::Product;
use crate::process::{CommandOutput, CommandSpec, Runner};
use crate::tooling::load_toolset;
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

use super::super::identity::{ReleaseIdentity, WorkflowContext, required_rows};

#[derive(Debug)]
struct ApiRunner {
    responses: BTreeMap<String, Vec<u8>>,
    calls: Vec<Vec<String>>,
    failed_endpoints: BTreeSet<String>,
}

impl ApiRunner {
    fn new(responses: BTreeMap<String, Vec<u8>>) -> Self {
        Self {
            responses,
            calls: Vec::new(),
            failed_endpoints: BTreeSet::new(),
        }
    }
}

impl Runner for ApiRunner {
    fn run(&mut self, spec: &CommandSpec) -> Result<CommandOutput> {
        let args = spec
            .args
            .iter()
            .map(|arg| arg.to_string_lossy().into_owned())
            .collect::<Vec<_>>();
        ensure!(
            args.len() == 2 && args[0] == "api",
            "unexpected GitHub CLI arguments: {args:?}"
        );
        let endpoint = args[1].clone();
        self.calls.push(args);
        if self.failed_endpoints.contains(&endpoint) {
            return Ok(CommandOutput {
                code: Some(1),
                stdout: Vec::new(),
                stderr: b"fixture command failure".to_vec(),
            });
        }
        let response = self
            .responses
            .get(&endpoint)
            .context("unexpected GitHub API endpoint")?
            .clone();
        Ok(CommandOutput {
            code: Some(0),
            stdout: response,
            stderr: Vec::new(),
        })
    }
}

fn context() -> WorkflowContext {
    WorkflowContext {
        repository: "example/adapters".into(),
        event: "workflow_dispatch".into(),
        source_ref: "refs/heads/main".into(),
        source_commit: "a".repeat(40),
        workflow_commit: "a".repeat(40),
        workflow_repository: "example/adapters".into(),
        workflow_ref: "example/adapters/.github/workflows/release.yml@refs/heads/main".into(),
        workflow_path: ".github/workflows/release.yml".into(),
        run_id: "123".into(),
        run_attempt: "2".into(),
    }
}

fn identity(product: Product) -> ReleaseIdentity {
    let tag_prefix = match product {
        Product::Foundation => "foundation",
        Product::SkyrimSe => "skyrim-se",
        Product::SkyrimLe => "skyrim-le",
        Product::Fallout4 => "fallout4",
    };
    ReleaseIdentity {
        schema_version: 1,
        repository: "example/adapters".into(),
        product,
        version: "1.2.3".into(),
        tag: format!("{tag_prefix}/v1.2.3"),
        source_commit: "b".repeat(40),
        workflow_commit: "a".repeat(40),
        run_id: "123".into(),
        run_attempt: "2".into(),
        notes: "- Current release".into(),
    }
}

fn api_responses(product: Product) -> Result<BTreeMap<String, Vec<u8>>> {
    let ctx = context();
    let id = identity(product);
    let run_id = ctx.run_id.parse::<u64>()?;
    let run = json!({
        "id": run_id,
        "run_attempt": 2,
        "event": "workflow_dispatch",
        "head_branch": "main",
        "head_sha": ctx.workflow_commit,
        "path": ".github/workflows/release.yml@main",
        "run_started_at": "2026-11-02T18:30:00Z"
    });
    let mut jobs = Vec::new();
    for (index, (row_product, architecture)) in required_rows(product).into_iter().enumerate() {
        jobs.push(json!({
            "id": 700_u64 + u64::try_from(index)?,
            "name": format!("Package and qualify ({row_product}, {architecture})"),
            "status": "completed",
            "conclusion": "success"
        }));
    }
    let mut artifacts = Vec::new();
    for (index, (row_product, architecture)) in required_rows(product).into_iter().enumerate() {
        let artifact_id = 500_u64
            .checked_add(u64::try_from(index)?)
            .context("fixture artifact ID overflow")?;
        artifacts.push(json!({
                "id": artifact_id,
                "name": format!("release-{row_product}-{architecture}-{}-{}", ctx.run_id, ctx.run_attempt),
                "digest": format!("sha256:{}", "c".repeat(64)),
                "size_in_bytes": 1024,
                "created_at": "2026-11-02T18:32:00Z",
                "expired": false,
                "workflow_run": { "id": run_id, "head_sha": ctx.workflow_commit.clone(), "head_branch": "main" },
                "release_tag": id.tag.clone()
            }));
    }
    Ok(BTreeMap::from([
        (
            format!(
                "repos/{}/commits/{}",
                ctx.repository,
                encode_path_segment(&id.tag)
            ),
            serde_json::to_vec(&json!({ "sha": id.source_commit }))?,
        ),
        (
            format!(
                "repos/{}/actions/runs/{run_id}/attempts/{}",
                ctx.repository, ctx.run_attempt
            ),
            serde_json::to_vec(&run)?,
        ),
        (
            format!(
                "repos/{}/actions/runs/{run_id}/attempts/{}/jobs?per_page=100",
                ctx.repository, ctx.run_attempt
            ),
            serde_json::to_vec(&json!({ "total_count": jobs.len(), "jobs": jobs }))?,
        ),
        (
            format!(
                "repos/{}/actions/runs/{run_id}/artifacts?per_page=100",
                ctx.repository
            ),
            serde_json::to_vec(&json!({ "total_count": artifacts.len(), "artifacts": artifacts }))?,
        ),
    ]))
}

fn load_test_toolset(directory: &Path) -> Result<crate::tooling::Toolset> {
    let executable = directory.join(if cfg!(windows) { "gh.exe" } else { "gh" });
    std::fs::write(&executable, b"test executable")?;
    let report = directory.join("toolset.json");
    let document = json!({
        "schema_version": 1,
        "profile": "release",
        "platform": if cfg!(windows) { "windows-x64" } else { "linux-x64" },
        "lock_sha256": "0".repeat(64),
        "tools": {
            "gh": { "version": "2.103.3", "path": executable, "kind": "executable", "archive": null, "sha256": "0".repeat(64) }
        },
        "environment": {}
    });
    std::fs::write(&report, serde_json::to_vec(&document)?)?;
    load_toolset(&report)
}

fn append_artifact(responses: &mut BTreeMap<String, Vec<u8>>, artifact: Value) -> Result<()> {
    let artifacts_endpoint = format!(
        "repos/{}/actions/runs/{}/artifacts?per_page=100",
        context().repository,
        context().run_id
    );
    let mut response: Value = crate::json::parse_slice(
        responses
            .get(&artifacts_endpoint)
            .context("artifact fixture response is missing")?,
    )?;
    let entries = response
        .get_mut("artifacts")
        .and_then(Value::as_array_mut)
        .context("artifact fixture has no list")?;
    entries.push(artifact);
    let count = entries.len();
    *response
        .get_mut("total_count")
        .context("artifact fixture count is missing")? = json!(count);
    responses.insert(artifacts_endpoint, serde_json::to_vec(&response)?);
    Ok(())
}

fn artifacts_endpoint() -> String {
    format!(
        "repos/{}/actions/runs/{}/artifacts?per_page=100",
        context().repository,
        context().run_id
    )
}

fn jobs_endpoint() -> String {
    format!(
        "repos/{}/actions/runs/{}/attempts/{}/jobs?per_page=100",
        context().repository,
        context().run_id,
        context().run_attempt
    )
}

fn install_pages(
    responses: &mut BTreeMap<String, Vec<u8>>,
    endpoint: &str,
    collection_key: &str,
    items: &[Value],
) -> Result<()> {
    responses.retain(|key, _| !key.starts_with(&format!("{endpoint}&page=")));
    let total = items.len();
    let page_count = total.div_ceil(100).max(1);
    for page_index in 0..page_count {
        let start = page_index * 100;
        let end = (start + 100).min(total);
        let page_items = if start < end {
            items[start..end].to_vec()
        } else {
            Vec::new()
        };
        let page_endpoint = if page_index == 0 {
            endpoint.to_owned()
        } else {
            format!("{endpoint}&page={}", page_index + 1)
        };
        let mut response = serde_json::Map::new();
        response.insert("total_count".into(), json!(total));
        response.insert(collection_key.into(), json!(page_items));
        responses.insert(page_endpoint, serde_json::to_vec(&Value::Object(response))?);
    }
    Ok(())
}

fn artifact_inventory_with_prior_attempts(
    responses: &mut BTreeMap<String, Vec<u8>>,
    prior_count: usize,
) -> Result<()> {
    let endpoint = artifacts_endpoint();
    let response: Value = crate::json::parse_slice(
        responses
            .get(&endpoint)
            .context("artifact fixture response is missing")?,
    )?;
    let current = response
        .get("artifacts")
        .and_then(Value::as_array)
        .context("artifact fixture has no list")?;
    let mut items = Vec::with_capacity(prior_count + current.len());
    for index in 0..prior_count {
        items.push(quality_diagnostic(
            "check-fallout4-x64-tests-123-1",
            10_000 + u64::try_from(index)?,
            "2026-11-02T18:20:00Z",
        ));
    }
    items.extend(current.iter().cloned());
    install_pages(responses, &endpoint, "artifacts", &items)
}

fn quality_diagnostic(name: &str, id: u64, created_at: &str) -> Value {
    json!({
        "id": id,
        "name": name,
        "digest": format!("sha256:{}", "d".repeat(64)),
        "size_in_bytes": 128,
        "created_at": created_at,
        "expired": false,
        "workflow_run": {
            "id": 123,
            "head_sha": "a".repeat(40),
            "head_branch": "main"
        }
    })
}

fn store_json_response(
    responses: &mut BTreeMap<String, Vec<u8>>,
    endpoint: &str,
    response: &Value,
) -> Result<()> {
    responses.insert(endpoint.to_owned(), serde_json::to_vec(response)?);
    Ok(())
}

fn run_current_selection_fails(
    responses: BTreeMap<String, Vec<u8>>,
    failed_endpoint: Option<&str>,
) -> Result<bool> {
    let temp = tempfile::tempdir()?;
    let toolset = load_test_toolset(temp.path())?;
    let mut runner = ApiRunner::new(responses);
    if let Some(endpoint) = failed_endpoint {
        runner.failed_endpoints.insert(endpoint.to_owned());
    }
    Ok(select_current_run(
        &context(),
        &identity(Product::Fallout4),
        &toolset,
        &mut runner,
    )
    .is_err())
}

#[test]
fn selects_exact_current_attempt_artifact_ids_and_digests() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let toolset = load_test_toolset(temp.path())?;
    let mut runner = ApiRunner::new(api_responses(Product::Fallout4)?);
    let receipt = select_current_run(
        &context(),
        &identity(Product::Fallout4),
        &toolset,
        &mut runner,
    )?;
    assert_eq!(receipt.artifacts.len(), 3);
    assert_eq!(receipt.artifacts[0].name, "release-foundation-x64-123-2");
    assert_eq!(receipt.artifacts[2].name, "release-fallout4-x64-123-2");
    assert_eq!(runner.calls.len(), 3);
    assert!(runner.calls.iter().all(|args| args[0] == "api"));
    assert!(
        runner
            .calls
            .iter()
            .all(|args| !args.iter().any(|arg| arg.contains("token")))
    );
    Ok(())
}

#[test]
fn paginates_prior_attempt_artifacts_before_selecting_later_current_artifacts() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let toolset = load_test_toolset(temp.path())?;
    let mut responses = api_responses(Product::Fallout4)?;
    artifact_inventory_with_prior_attempts(&mut responses, 100)?;
    let mut runner = ApiRunner::new(responses);

    let receipt = select_current_run(
        &context(),
        &identity(Product::Fallout4),
        &toolset,
        &mut runner,
    )?;

    assert_eq!(receipt.artifacts.len(), 3);
    assert!(receipt.artifacts.iter().all(|artifact| artifact.id >= 500));
    assert!(runner.calls.iter().any(|args| {
        args.get(1)
            .is_some_and(|endpoint| endpoint.ends_with("/artifacts?per_page=100&page=2"))
    }));
    Ok(())
}

#[test]
fn paginates_current_attempt_jobs_before_validating_required_matrix_rows() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let toolset = load_test_toolset(temp.path())?;
    let mut responses = api_responses(Product::Fallout4)?;
    let endpoint = jobs_endpoint();
    let response: Value = crate::json::parse_slice(
        responses
            .get(&endpoint)
            .context("jobs fixture response is missing")?,
    )?;
    let mut jobs = response
        .get("jobs")
        .and_then(Value::as_array)
        .context("jobs fixture has no list")?
        .clone();
    for index in 0..98 {
        jobs.push(json!({
            "id": 8_000 + index,
            "name": format!("Additional current-attempt job {index}"),
            "status": "completed",
            "conclusion": "success"
        }));
    }
    install_pages(&mut responses, &endpoint, "jobs", &jobs)?;
    let mut runner = ApiRunner::new(responses);

    let receipt = select_current_run(
        &context(),
        &identity(Product::Fallout4),
        &toolset,
        &mut runner,
    )?;

    assert_eq!(receipt.artifacts.len(), 3);
    assert!(runner.calls.iter().any(|args| {
        args.get(1)
            .is_some_and(|endpoint| endpoint.ends_with("/jobs?per_page=100&page=2"))
    }));
    Ok(())
}

#[test]
fn rejects_inconsistent_or_incomplete_paginated_artifact_inventories() -> Result<()> {
    let mut baseline = api_responses(Product::Fallout4)?;
    artifact_inventory_with_prior_attempts(&mut baseline, 100)?;
    let endpoint = artifacts_endpoint();
    let continuation = format!("{endpoint}&page=2");

    let mut duplicate_id = baseline.clone();
    let mut second_page: Value = crate::json::parse_slice(&duplicate_id[&continuation])?;
    second_page["artifacts"][0]["id"] = json!(10_000);
    store_json_response(&mut duplicate_id, &continuation, &second_page)?;
    assert!(run_current_selection_fails(duplicate_id, None)?);

    let mut changing_total = baseline.clone();
    let mut second_page: Value = crate::json::parse_slice(&changing_total[&continuation])?;
    second_page["total_count"] = json!(104);
    store_json_response(&mut changing_total, &continuation, &second_page)?;
    assert!(run_current_selection_fails(changing_total, None)?);

    let mut missing_continuation = baseline.clone();
    missing_continuation.remove(&continuation);
    assert!(run_current_selection_fails(missing_continuation, None)?);

    let mut empty_continuation = baseline.clone();
    let mut second_page: Value = crate::json::parse_slice(&empty_continuation[&continuation])?;
    second_page["artifacts"] = json!([]);
    store_json_response(&mut empty_continuation, &continuation, &second_page)?;
    assert!(run_current_selection_fails(empty_continuation, None)?);

    let mut short_first_page = baseline.clone();
    let mut first_page: Value = crate::json::parse_slice(&short_first_page[&endpoint])?;
    first_page["artifacts"]
        .as_array_mut()
        .context("artifact list is an array")?
        .pop();
    store_json_response(&mut short_first_page, &endpoint, &first_page)?;
    assert!(run_current_selection_fails(short_first_page, None)?);

    let mut oversized_first_page = baseline.clone();
    let mut first_page: Value = crate::json::parse_slice(&oversized_first_page[&endpoint])?;
    first_page["artifacts"]
        .as_array_mut()
        .context("artifact list is an array")?
        .push(quality_diagnostic(
            "check-fallout4-x64-tests-123-1",
            20_000,
            "2026-11-02T18:20:00Z",
        ));
    store_json_response(&mut oversized_first_page, &endpoint, &first_page)?;
    assert!(run_current_selection_fails(oversized_first_page, None)?);
    Ok(())
}

#[test]
fn rejects_later_page_command_and_json_failures() -> Result<()> {
    let mut baseline = api_responses(Product::Fallout4)?;
    artifact_inventory_with_prior_attempts(&mut baseline, 100)?;
    let continuation = format!("{}&page=2", artifacts_endpoint());

    assert!(run_current_selection_fails(
        baseline.clone(),
        Some(continuation.as_str())
    )?);

    let mut invalid_json = baseline;
    invalid_json.insert(continuation, b"{".to_vec());
    assert!(run_current_selection_fails(invalid_json, None)?);
    Ok(())
}

#[test]
fn excessive_inventory_is_rejected_before_following_pages() -> Result<()> {
    let mut calls = 0;
    let result = super::pagination::fetch_all_pages(
        "repos/example/adapters/actions/runs/123/artifacts?per_page=100",
        "artifacts",
        |_| {
            calls += 1;
            Ok(json!({ "total_count": 10_001, "artifacts": [] }))
        },
    );
    let error = result.err().context("excessive inventory was accepted")?;
    assert!(error.to_string().contains("pagination limit"));
    assert_eq!(calls, 1);
    Ok(())
}

#[test]
fn ignores_recognized_previous_attempt_artifacts_only() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let toolset = load_test_toolset(temp.path())?;
    let mut responses = api_responses(Product::Fallout4)?;
    let artifacts_endpoint = format!(
        "repos/{}/actions/runs/{}/artifacts?per_page=100",
        context().repository,
        context().run_id
    );
    let mut artifacts: Value = crate::json::parse_slice(
        responses
            .get(&artifacts_endpoint)
            .context("artifact fixture response is missing")?,
    )?;
    let entries = artifacts
        .get_mut("artifacts")
        .and_then(Value::as_array_mut)
        .context("artifact fixture has no list")?;
    entries.push(json!({
        "id": 499,
        "name": "release-foundation-x64-123-1",
        "digest": format!("sha256:{}", "d".repeat(64)),
        "size_in_bytes": 1024,
        "created_at": "2026-11-02T18:20:00Z",
        "expired": false,
        "workflow_run": { "id": 123, "head_sha": "a".repeat(40), "head_branch": "main" }
    }));
    let count = entries.len();
    *artifacts
        .get_mut("total_count")
        .context("artifact fixture count is missing")? = json!(count);
    responses.insert(artifacts_endpoint, serde_json::to_vec(&artifacts)?);

    let mut runner = ApiRunner::new(responses);
    let receipt = select_current_run(
        &context(),
        &identity(Product::Fallout4),
        &toolset,
        &mut runner,
    )?;
    assert_eq!(receipt.artifacts.len(), 3);
    let rows = BTreeSet::from(["foundation-x64".to_owned()]);
    assert!(is_prior_attempt_artifact(
        "release-foundation-x64-123-1",
        "123",
        "2",
        &rows,
    )?);
    assert!(!is_prior_attempt_artifact(
        "release-foundation-x64-123-3",
        "123",
        "2",
        &rows,
    )?);
    assert!(!is_prior_attempt_artifact(
        "release-skyrim-se-x64-123-1",
        "123",
        "2",
        &rows,
    )?);
    Ok(())
}

#[test]
fn ignores_supported_quality_diagnostics_from_current_and_prior_attempts() -> Result<()> {
    let temp = tempfile::tempdir()?;
    let toolset = load_test_toolset(temp.path())?;
    let mut responses = api_responses(Product::Fallout4)?;
    append_artifact(
        &mut responses,
        quality_diagnostic(
            "check-fallout4-x64-tests-123-2",
            610,
            "2026-11-02T18:32:00Z",
        ),
    )?;
    append_artifact(
        &mut responses,
        quality_diagnostic(
            "check-fallout4-x64-tests-123-1",
            611,
            "2026-11-02T18:20:00Z",
        ),
    )?;
    let mut runner = ApiRunner::new(responses);

    let receipt = select_current_run(
        &context(),
        &identity(Product::Fallout4),
        &toolset,
        &mut runner,
    )?;
    assert_eq!(receipt.artifacts.len(), 3);
    assert!(
        receipt
            .artifacts
            .iter()
            .all(|artifact| artifact.name.starts_with("release-"))
    );
    Ok(())
}

#[test]
fn rejects_future_wrong_run_and_unknown_quality_diagnostic_names() -> Result<()> {
    for (name, workflow_run_id) in [
        ("check-fallout4-x64-tests-123-3", None),
        ("check-fallout4-x64-tests-999-2", None),
        ("check-fallout4-x64-unknown-123-2", None),
        ("check-skyrim-se-x86-tests-123-2", None),
        ("check-fallout4-x64-tests-123-2", Some(999)),
    ] {
        let temp = tempfile::tempdir()?;
        let toolset = load_test_toolset(temp.path())?;
        let mut responses = api_responses(Product::Fallout4)?;
        let mut diagnostic = quality_diagnostic(name, 620, "2026-11-02T18:32:00Z");
        if let Some(run_id) = workflow_run_id {
            diagnostic["workflow_run"]["id"] = json!(run_id);
        }
        append_artifact(&mut responses, diagnostic)?;
        let mut runner = ApiRunner::new(responses);

        assert!(
            select_current_run(
                &context(),
                &identity(Product::Fallout4),
                &toolset,
                &mut runner,
            )
            .is_err(),
            "unexpectedly accepted diagnostic artifact '{name}'"
        );
    }
    Ok(())
}

#[test]
fn matrix_rows_cover_full_foundation_and_required_adapter_pairings() {
    assert_eq!(required_rows(Product::Foundation).len(), 5);
    assert_eq!(required_rows(Product::SkyrimSe).len(), 3);
    assert_eq!(required_rows(Product::SkyrimLe).len(), 3);
    assert_eq!(required_rows(Product::Fallout4).len(), 3);
}

#[test]
fn product_tag_ref_is_encoded_as_one_commit_endpoint_segment() {
    assert_eq!(
        encode_path_segment("foundation/v1.2.3"),
        "foundation%2Fv1.2.3"
    );
}

#[test]
fn rejects_failed_missing_or_duplicate_required_jobs() -> Result<()> {
    let good = api_responses(Product::SkyrimSe)?;
    let job_path = "repos/example/adapters/actions/runs/123/attempts/2/jobs?per_page=100";
    let jobs: Value = serde_json::from_slice(&good[job_path])?;
    validate_jobs(&jobs, Product::SkyrimSe)?;

    let mut failed = jobs.clone();
    failed["jobs"][0]["conclusion"] = Value::String("failure".into());
    assert!(validate_jobs(&failed, Product::SkyrimSe).is_err());

    let mut missing = jobs.clone();
    missing["jobs"]
        .as_array_mut()
        .context("jobs fixture is an array")?
        .pop();
    missing["total_count"] = json!(2);
    assert!(validate_jobs(&missing, Product::SkyrimSe).is_err());

    let mut duplicate = jobs;
    let first = duplicate["jobs"][0].clone();
    duplicate["jobs"]
        .as_array_mut()
        .context("jobs fixture is an array")?
        .push(first);
    duplicate["total_count"] = json!(4);
    assert!(validate_jobs(&duplicate, Product::SkyrimSe).is_err());
    Ok(())
}

#[test]
fn service_run_identity_requires_main_manual_current_attempt() -> Result<()> {
    let responses = api_responses(Product::Foundation)?;
    let mut run: Value =
        serde_json::from_slice(&responses["repos/example/adapters/actions/runs/123/attempts/2"])?;
    validate_run(&run, &context())?;
    run["path"] = json!(".github/workflows/release.yml");
    validate_run(&run, &context())?;
    run["path"] = json!(".github/workflows/release.yml@refs/heads/main");
    validate_run(&run, &context())?;
    run["run_attempt"] = json!(1);
    assert!(validate_run(&run, &context()).is_err());
    run["run_attempt"] = json!(2);
    run["head_sha"] = Value::String("d".repeat(40));
    assert!(validate_run(&run, &context()).is_err());
    Ok(())
}
