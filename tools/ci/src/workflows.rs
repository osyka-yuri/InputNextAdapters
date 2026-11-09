//! Workflow syntax, shell analysis, and release authority policy.

mod shell;
#[cfg(test)]
mod tests;

use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use clap::Args;
use serde_yaml_ng::Value;

use crate::process::{CommandSpec, Runner, run_checked};
use crate::tooling::load_toolset;

const WORKFLOWS: &[&str] = &["ci.yml", "quality.yml", "release.yml"];
const ACTIONS: &[&str] = &[
    "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1",
    "actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9",
    "actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9",
    "actions/download-artifact@9000827ccba6bdab643e8b6fd33ac0654aef8333",
];
// The pinned actionlint schema predates defining-workflow identity fields.
const CONTEXT_IGNORE: &str = r#"^property "(workflow_sha|workflow_repository|workflow_ref|workflow_file_path)" is not defined in object type \{check_run_id:"#;
const MANUAL_MAIN: &str =
    "${{ github.event_name == 'workflow_dispatch' && github.ref == 'refs/heads/main' }}";

/// Validate the repository's workflows with pinned host tools.
#[derive(Debug, Args)]
pub struct WorkflowArgs {
    #[arg(long, default_value = ".")]
    root: PathBuf,
    #[arg(long)]
    toolset: PathBuf,
}

impl WorkflowArgs {
    /// Checks policy, actionlint and Bash scripts without executing workflow payloads.
    ///
    /// # Errors
    /// Returns an error for invalid policy, YAML, shell or tool execution.
    pub fn run(self, runner: &mut dyn Runner) -> Result<()> {
        let root = dunce::canonicalize(self.root).context("resolve repository")?;
        check_repository(&root)?;
        let tools = load_toolset(&self.toolset)?;
        run_checked(
            runner,
            &CommandSpec {
                program: tools.executable("actionlint")?.to_path_buf(),
                args: vec![
                    OsString::from(format!(
                        "-shellcheck={}",
                        tools.executable("shellcheck")?.display()
                    )),
                    OsString::from("-ignore"),
                    OsString::from(CONTEXT_IGNORE),
                ],
                cwd: root.clone(),
                env: BTreeMap::new(),
                timeout: Duration::from_secs(120),
                output_limit: 4 * 1024 * 1024,
            },
        )?;
        shell::check(&root, tools.executable("shellcheck")?, runner)?;
        println!("Workflow policy, actionlint and Bash checks passed.");
        Ok(())
    }
}

/// Checks the maintained workflow and composite-action authority boundaries.
///
/// # Errors
/// Returns an error for missing files, malformed YAML or unapproved authority.
pub fn check_repository(root: &Path) -> Result<()> {
    for name in WORKFLOWS {
        let document = read_yaml(&root.join(".github/workflows").join(name))?;
        check_workflow(name, &document)?;
    }
    check_composites(root)
}

fn read_yaml(path: &Path) -> Result<Value> {
    let metadata =
        fs::symlink_metadata(path).with_context(|| format!("inspect {}", path.display()))?;
    ensure!(
        metadata.is_file() && metadata.len() <= 1024 * 1024,
        "workflow must be a bounded regular file"
    );
    serde_yaml_ng::from_str(&fs::read_to_string(path)?)
        .with_context(|| format!("parse {}", path.display()))
}

fn check_workflow(name: &str, document: &Value) -> Result<()> {
    let events = document["on"]
        .as_mapping()
        .context("workflow requires explicit events")?;
    for event in ["pull_request_target", "workflow_run"] {
        ensure!(
            !events.contains_key(Value::from(event)),
            "{name}: forbidden authority event {event}"
        );
    }
    let permissions = document["permissions"]
        .as_mapping()
        .context("workflow requires explicit permissions")?;
    ensure!(
        permissions.len() == 1 && document["permissions"]["contents"].as_str() == Some("read"),
        "{name}: default permissions must be contents read only"
    );
    if name == "ci.yml" {
        let branches = document["on"]["push"]["branches"]
            .as_sequence()
            .context("push must be restricted to main")?;
        ensure!(
            branches.as_slice() == [Value::from("main")],
            "branch pushes duplicate PR runs"
        );
    }
    if name == "release.yml" {
        ensure!(
            events.len() == 1 && events.contains_key(Value::from("workflow_dispatch")),
            "release authority requires manual dispatch only"
        );
    }
    let jobs = document["jobs"]
        .as_mapping()
        .context("workflow requires jobs")?;
    for (key, job) in jobs {
        let job_name = key.as_str().context("job name must be text")?;
        let privileged = name == "release.yml" && job_name == "draft";
        check_job_permissions(name, job_name, job, privileged)?;
        if let Some(reusable) = job["uses"].as_str() {
            ensure!(
                matches!(name, "ci.yml" | "release.yml")
                    && reusable == "./.github/workflows/quality.yml",
                "unexpected reusable workflow"
            );
            if name == "release.yml" {
                ensure!(
                    job["with"]["source_ref"].as_str()
                        == Some("${{ needs.qualify.outputs.source_commit }}"),
                    "release checks must use the qualified tag source"
                );
            }
            continue;
        }
        let steps = job["steps"].as_sequence().context("job requires steps")?;
        for step in steps {
            check_step(step)?;
        }
        if name == "release.yml" && matches!(job_name, "qualify" | "draft") {
            ensure!(
                job["if"].as_str() == Some(MANUAL_MAIN),
                "authority job must be restricted to manual main"
            );
            check_authority_steps(steps, privileged)?;
        }
        if privileged {
            ensure!(
                job["permissions"]["contents"].as_str() == Some("write"),
                "draft job needs explicit write authority"
            );
            let needs = job["needs"]
                .as_sequence()
                .context("draft must require completed qualification and checks")?;
            for required in ["qualify", "quality", "packages"] {
                ensure!(
                    needs.contains(&Value::from(required)),
                    "draft is missing required predecessor {required}"
                );
            }
            ensure!(
                job["concurrency"]["cancel-in-progress"].as_bool() == Some(false),
                "draft retries must not cancel another draft upload"
            );
        }
    }
    Ok(())
}

fn check_job_permissions(name: &str, job_name: &str, job: &Value, privileged: bool) -> Result<()> {
    if let Some(grants) = job["permissions"].as_mapping() {
        for (permission, level) in grants {
            let permission = permission
                .as_str()
                .context("permission name must be text")?;
            let level = level
                .as_str()
                .context("permission level must be explicit")?;
            ensure!(
                matches!((permission, level), ("contents" | "actions", "read"))
                    || (privileged && permission == "contents" && level == "write"),
                "{name}/{job_name}: unexpected permission {permission}:{level}"
            );
        }
    }
    ensure!(
        job["secrets"].is_null(),
        "workflow jobs must not inherit repository secrets"
    );
    Ok(())
}

fn check_step(step: &Value) -> Result<()> {
    if let Some(action) = step["uses"].as_str() {
        let local = [
            "./.github/actions/bootstrap",
            "./.github/actions/source",
            "./trusted/.github/actions/bootstrap",
            "./trusted/.github/actions/source",
        ];
        ensure!(
            ACTIONS.contains(&action) || local.contains(&action),
            "action is not an approved immutable action: {action}"
        );
        if action.starts_with("actions/checkout@") {
            ensure!(
                step["with"]["persist-credentials"].as_bool() == Some(false),
                "checkout must not persist credentials"
            );
        }
    }
    if let Some(script) = step["run"].as_str() {
        ensure!(
            !script.contains("${{"),
            "pass expression data through environment variables, not executable text"
        );
    }
    Ok(())
}

fn check_authority_steps(steps: &[Value], privileged: bool) -> Result<()> {
    let mut qualified = false;
    let mut bootstrapped = false;
    for step in steps {
        if let Some(action) = step["uses"].as_str() {
            if action == "./trusted/.github/actions/source" {
                ensure!(!bootstrapped, "source guard must precede bootstrap");
                qualified = true;
            }
            if action == "./trusted/.github/actions/bootstrap" {
                ensure!(
                    qualified && step["with"]["cache"].as_str() == Some("false"),
                    "authority bootstrap requires qualified source and no executable cache"
                );
                bootstrapped = true;
            }
            if action.starts_with("actions/checkout@") {
                ensure!(
                    step["with"]["ref"].as_str() == Some("${{ job.workflow_sha }}")
                        && step["with"]["repository"].as_str()
                            == Some("${{ job.workflow_repository }}")
                        && step["with"]["path"].as_str() == Some("trusted"),
                    "authority must check out its own immutable workflow source"
                );
            }
            ensure!(
                !action.starts_with("actions/cache@"),
                "authority jobs cannot restore executable caches"
            );
            if action.starts_with("actions/download-artifact@") {
                ensure!(
                    privileged
                        && step["with"]["artifact-ids"].as_str()
                            == Some("${{ steps.artifacts.outputs.artifact_ids }}")
                        && step["with"]["digest-mismatch"].as_str() == Some("error"),
                    "release artifacts must use independently selected IDs and strict digests"
                );
            }
        }
        if let Some(script) = step["run"].as_str() {
            ensure!(
                qualified && bootstrapped,
                "release helpers require qualified bootstrap"
            );
            if privileged {
                let step_id = step["id"]
                    .as_str()
                    .context("draft run steps require fixed reviewed IDs")?;
                ensure!(
                    matches!(step_id, "artifacts" | "publish"),
                    "unreviewed executable step in draft job"
                );
                let command = if step_id == "artifacts" {
                    "release artifacts"
                } else {
                    "release draft"
                };
                ensure!(
                    script.contains(command),
                    "unexpected draft authority command"
                );
            }
        }
    }
    ensure!(
        qualified && bootstrapped,
        "source qualification or bootstrap is missing"
    );
    Ok(())
}

fn check_composites(root: &Path) -> Result<()> {
    for name in ["bootstrap", "source"] {
        let action = read_yaml(&root.join(".github/actions").join(name).join("action.yml"))?;
        ensure!(
            action["runs"]["using"].as_str() == Some("composite"),
            "local action must be composite"
        );
        for step in action["runs"]["steps"]
            .as_sequence()
            .context("composite action needs steps")?
        {
            check_step(step)?;
        }
    }
    let bootstrap = read_yaml(&root.join(".github/actions/bootstrap/action.yml"))?;
    let steps = bootstrap["runs"]["steps"]
        .as_sequence()
        .context("bootstrap needs steps")?;
    let cache = steps
        .iter()
        .find(|step| {
            step["uses"]
                .as_str()
                .is_some_and(|action| action.starts_with("actions/cache@"))
        })
        .context("archive cache step is missing")?;
    ensure!(
        cache["if"].as_str() == Some("${{ inputs.cache == 'true' }}")
            && cache["with"]["path"].as_str()
                == Some("${{ runner.temp }}/inputnext-adapters-tool-archives"),
        "bootstrap may cache archive bytes only and must honor cache=false"
    );
    let build = steps
        .iter()
        .find(|step| step["id"].as_str() == Some("build"))
        .context("locked CLI build step is missing")?;
    let command = build["run"]
        .as_str()
        .context("CLI build script is missing")?;
    ensure!(
        command.contains(
            "cargo build --release --locked --package inputnext-adapters-ci --target-dir"
        ),
        "bootstrap must build the locked CLI from source"
    );
    let source = read_yaml(&root.join(".github/actions/source/action.yml"))?;
    let guard = &source["runs"]["steps"][0];
    for (key, value) in [
        ("SOURCE_SHA", "${{ github.sha }}"),
        ("SOURCE_REF", "${{ github.ref }}"),
        ("SOURCE_REPOSITORY", "${{ github.repository }}"),
        ("WORKFLOW_SHA", "${{ job.workflow_sha }}"),
        ("WORKFLOW_REPOSITORY", "${{ job.workflow_repository }}"),
        ("WORKFLOW_REF", "${{ job.workflow_ref }}"),
        ("WORKFLOW_PATH", "${{ job.workflow_file_path }}"),
        ("EVENT", "${{ github.event_name }}"),
    ] {
        ensure!(
            guard["env"][key].as_str() == Some(value),
            "source guard must obtain {key} from the current workflow"
        );
    }
    ensure!(
        guard["run"].as_str() == Some("bash \"$GITHUB_ACTION_PATH/qualify.sh\""),
        "source action must run the owned guard"
    );
    ensure!(
        root.join(".github/actions/source/qualify.sh").is_file(),
        "source guard is missing"
    );
    Ok(())
}
