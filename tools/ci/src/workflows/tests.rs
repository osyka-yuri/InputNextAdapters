use anyhow::Result;
use serde_yaml_ng::Value;

use super::{check_step, check_workflow};

fn ordinary() -> Result<Value> {
    Ok(serde_yaml_ng::from_str(
        r"
on:
  pull_request:
  push:
    branches: [main]
permissions:
  contents: read
jobs:
  tests:
    runs-on: windows-2025-vs2026
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1
        with:
          persist-credentials: false
      - run: echo checked
",
    )?)
}

#[test]
fn pr_jobs_cannot_gain_release_permissions_or_inherited_secrets() -> Result<()> {
    let valid = ordinary()?;
    check_workflow("ci.yml", &valid)?;
    let mut changed = valid.clone();
    changed["jobs"]["tests"]["permissions"] = serde_yaml_ng::from_str("contents: write")?;
    assert!(check_workflow("ci.yml", &changed).is_err());
    let mut changed = valid;
    changed["jobs"]["tests"]["secrets"] = Value::from("inherit");
    assert!(check_workflow("ci.yml", &changed).is_err());
    Ok(())
}

#[test]
fn privileged_trigger_bridges_and_duplicate_branch_pushes_are_rejected() -> Result<()> {
    let mut changed = ordinary()?;
    changed["on"]["pull_request_target"] = Value::Null;
    assert!(check_workflow("ci.yml", &changed).is_err());
    let mut changed = ordinary()?;
    changed["on"]["workflow_run"] = serde_yaml_ng::from_str("workflows: [CI]")?;
    assert!(check_workflow("ci.yml", &changed).is_err());
    let mut changed = ordinary()?;
    changed["on"]["push"]["branches"] = Value::Sequence(vec![Value::from("**")]);
    assert!(check_workflow("ci.yml", &changed).is_err());
    Ok(())
}

#[test]
fn interpolated_code_mutable_actions_and_persisted_credentials_fail() -> Result<()> {
    for text in [
        "run: echo '${{ github.event.pull_request.title }}'",
        "uses: actions/checkout@main",
        "uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1\nwith:\n  persist-credentials: true",
    ] {
        let step: Value = serde_yaml_ng::from_str(text)?;
        assert!(check_step(&step).is_err(), "accepted {text}");
    }
    Ok(())
}

#[test]
fn release_authority_requires_manual_dispatch() -> Result<()> {
    assert!(check_workflow("release.yml", &ordinary()?).is_err());
    Ok(())
}

fn release() -> Result<Value> {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    Ok(serde_yaml_ng::from_str(&std::fs::read_to_string(
        root.join(".github/workflows/release.yml"),
    )?)?)
}

fn change_step(
    document: &mut Value,
    action_prefix: &str,
    edit: impl FnOnce(&mut Value),
) -> Result<()> {
    let steps = document["jobs"]["draft"]["steps"]
        .as_sequence_mut()
        .ok_or_else(|| anyhow::anyhow!("draft steps are missing"))?;
    let step = steps
        .iter_mut()
        .find(|step| {
            step["uses"]
                .as_str()
                .is_some_and(|action| action.starts_with(action_prefix))
        })
        .ok_or_else(|| anyhow::anyhow!("step {action_prefix} is missing"))?;
    edit(step);
    Ok(())
}

#[test]
fn release_requires_qualified_main_source_and_uncached_bootstrap() -> Result<()> {
    let mut changed = release()?;
    change_step(&mut changed, "actions/checkout@", |step| {
        step["with"]["ref"] = Value::from("${{ inputs.tag }}");
    })?;
    assert!(check_workflow("release.yml", &changed).is_err());
    let mut changed = release()?;
    change_step(
        &mut changed,
        "./trusted/.github/actions/bootstrap",
        |step| step["with"]["cache"] = Value::from("true"),
    )?;
    assert!(check_workflow("release.yml", &changed).is_err());
    let mut changed = release()?;
    changed["jobs"]["draft"]["if"] = Value::Bool(true);
    assert!(check_workflow("release.yml", &changed).is_err());
    Ok(())
}

#[test]
fn release_artifact_identity_and_mandatory_predecessors_cannot_be_weakened() -> Result<()> {
    let mut changed = release()?;
    change_step(&mut changed, "actions/download-artifact@", |step| {
        step["with"]["artifact-ids"] = Value::Null;
        step["with"]["name"] = Value::from("producer-selected-name");
    })?;
    assert!(check_workflow("release.yml", &changed).is_err());
    let mut changed = release()?;
    change_step(&mut changed, "actions/download-artifact@", |step| {
        step["with"]["digest-mismatch"] = Value::from("warn");
    })?;
    assert!(check_workflow("release.yml", &changed).is_err());
    let mut changed = release()?;
    changed["jobs"]["draft"]["needs"] = Value::Sequence(vec![Value::from("packages")]);
    assert!(check_workflow("release.yml", &changed).is_err());
    Ok(())
}

#[test]
fn authority_jobs_reject_extra_execution_and_the_quality_gate_uses_tag_source() -> Result<()> {
    let mut changed = release()?;
    changed["jobs"]["draft"]["steps"]
        .as_sequence_mut()
        .ok_or_else(|| anyhow::anyhow!("draft steps missing"))?
        .push(serde_yaml_ng::from_str(
            "id: execute-producer\nrun: ./release-data/producer-tool",
        )?);
    assert!(check_workflow("release.yml", &changed).is_err());
    let mut changed = release()?;
    changed["jobs"]["quality"]["with"]["source_ref"] = Value::from("main");
    assert!(check_workflow("release.yml", &changed).is_err());
    Ok(())
}
