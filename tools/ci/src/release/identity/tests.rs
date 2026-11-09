use super::{WorkflowContext, parse_tag, qualify, version_notes};
use crate::process::SystemRunner;
use std::path::{Path, PathBuf};
use std::process::Command;

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

#[test]
fn only_manual_main_context_is_authoritative() -> anyhow::Result<()> {
    context().validate()?;
    for field in ["event", "ref", "workflow", "repository", "run"] {
        let mut value = context();
        match field {
            "event" => value.event = "pull_request".into(),
            "ref" => value.source_ref = "refs/tags/skyrim-se/v0.1.0".into(),
            "workflow" => value.workflow_commit = "b".repeat(40),
            "repository" => value.workflow_repository = "other/repository".into(),
            "run" => value.run_attempt = "0".into(),
            _ => unreachable!(),
        }
        assert!(value.validate().is_err(), "accepted changed {field}");
    }
    Ok(())
}

#[test]
fn tags_require_a_known_product_and_stable_version() {
    for tag in [
        "v1.0.0",
        "skyrim/v1.0.0",
        "skyrim-se/v01.0.0",
        "fallout4/v1.0.0-rc.1",
        "foundation/v1.0.0+build",
        "skyrim-le/v1.0.0\n",
    ] {
        assert!(parse_tag(tag).is_err(), "accepted {tag:?}");
    }
    assert!(parse_tag("skyrim-se/v1.2.3").is_ok());
    assert!(parse_tag("skyrim-le/v1.2.3").is_ok());
}

#[test]
fn notes_are_product_version_scoped() -> anyhow::Result<()> {
    let notes = version_notes(
        "# Changes\n\n## [1.0.0]\n\n- Current.\n\n## [0.1.0]\n- Old.\n",
        "1.0.0",
    )?;
    assert_eq!(notes, "- Current.");
    assert!(version_notes("# Changes\n", "1.0.0").is_err());
    assert!(
        version_notes(
            "## [1.0.0]\n- First.\n## [0.9.0]\n- Old.\n## [1.0.0]\n- Duplicate.\n",
            "1.0.0"
        )
        .is_err()
    );
    assert!(version_notes("## [1.0.0]\n\n## [0.9.0]\n- Old.\n", "1.0.0").is_err());
    Ok(())
}

#[test]
fn qualifies_exact_ancestor_tag_in_an_isolated_git_repository() -> anyhow::Result<()> {
    let root = tempfile::tempdir()?;
    let git = git_executable()?;
    initialize_git_repository(&git, root.path())?;
    std::fs::write(
        root.path().join("CMakeLists.txt"),
        "project(InputNextAdapters VERSION 1.2.3 LANGUAGES CXX)\n",
    )?;
    std::fs::write(
        root.path().join("CHANGELOG.md"),
        "# Changelog\n\n## [1.2.3]\n\n- Release notes.\n",
    )?;
    git_setup(
        &git,
        root.path(),
        &["add", "CMakeLists.txt", "CHANGELOG.md"],
    )?;
    git_setup(&git, root.path(), &["commit", "-m", "release source"])?;
    git_setup(&git, root.path(), &["tag", "foundation/v1.2.3"])?;
    let commit = git_setup(&git, root.path(), &["rev-parse", "HEAD"])?;
    git_setup(
        &git,
        root.path(),
        &["update-ref", "refs/remotes/origin/main", &commit],
    )?;

    let mut context = context();
    context.source_commit = commit.clone();
    context.workflow_commit = commit;
    let mut runner = SystemRunner;
    let identity = qualify(
        &context,
        root.path(),
        &git,
        "foundation/v1.2.3",
        &mut runner,
    )?;

    assert_eq!(identity.product, crate::model::Product::Foundation);
    assert_eq!(identity.version, "1.2.3");
    assert_eq!(identity.source_commit, context.workflow_commit);
    assert_eq!(identity.notes, "- Release notes.");
    Ok(())
}

#[test]
fn rejects_tag_commit_outside_trusted_main_ancestry() -> anyhow::Result<()> {
    let root = tempfile::tempdir()?;
    let git = git_executable()?;
    initialize_git_repository(&git, root.path())?;
    std::fs::write(
        root.path().join("CMakeLists.txt"),
        "project(InputNextAdapters VERSION 1.2.3 LANGUAGES CXX)\n",
    )?;
    std::fs::write(
        root.path().join("CHANGELOG.md"),
        "## [1.2.3]\n- Release notes.\n",
    )?;
    git_setup(&git, root.path(), &["add", "."])?;
    git_setup(&git, root.path(), &["commit", "-m", "tag source"])?;
    let tagged = git_setup(&git, root.path(), &["rev-parse", "HEAD"])?;
    git_setup(&git, root.path(), &["tag", "foundation/v1.2.3"])?;
    git_setup(&git, root.path(), &["checkout", "--orphan", "main"])?;
    git_setup(&git, root.path(), &["rm", "-rf", "."])?;
    std::fs::write(
        root.path().join("CMakeLists.txt"),
        "project(InputNextAdapters VERSION 1.2.3 LANGUAGES CXX)\n",
    )?;
    std::fs::write(
        root.path().join("CHANGELOG.md"),
        "## [1.2.3]\n- Different main notes.\n",
    )?;
    git_setup(&git, root.path(), &["add", "."])?;
    git_setup(&git, root.path(), &["commit", "-m", "trusted main"])?;
    let main = git_setup(&git, root.path(), &["rev-parse", "HEAD"])?;
    git_setup(
        &git,
        root.path(),
        &["update-ref", "refs/remotes/origin/main", &main],
    )?;

    let mut context = context();
    context.source_commit = main.clone();
    context.workflow_commit = main;
    let mut runner = SystemRunner;
    let result = qualify(
        &context,
        root.path(),
        &git,
        "foundation/v1.2.3",
        &mut runner,
    );
    assert!(result.is_err(), "accepted unrelated tag commit {tagged}");
    Ok(())
}

fn git_executable() -> anyhow::Result<PathBuf> {
    let names = if cfg!(windows) {
        ["git.exe", "git"]
    } else {
        ["git", "git"]
    };
    for directory in std::env::split_paths(&std::env::var_os("PATH").unwrap_or_default()) {
        for name in names {
            let candidate = directory.join(name);
            if candidate.is_file() {
                return Ok(dunce::canonicalize(candidate)?);
            }
        }
    }
    anyhow::bail!("Git executable is required for the isolated release identity tests")
}

fn initialize_git_repository(git: &Path, root: &Path) -> anyhow::Result<()> {
    git_setup(git, root, &["init", "--initial-branch=tag-source"])?;
    git_setup(git, root, &["config", "user.name", "Release Test"])?;
    git_setup(
        git,
        root,
        &["config", "user.email", "release-test@example.invalid"],
    )?;
    Ok(())
}

fn git_setup(git: &Path, root: &Path, args: &[&str]) -> anyhow::Result<String> {
    let output = Command::new(git)
        .args(args)
        .current_dir(root)
        .env("GIT_CONFIG_NOSYSTEM", "1")
        .env("GIT_CONFIG_GLOBAL", root.join("empty-global-config"))
        .output()?;
    anyhow::ensure!(
        output.status.success(),
        "isolated Git fixture setup failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(String::from_utf8(output.stdout)?.trim().to_owned())
}
