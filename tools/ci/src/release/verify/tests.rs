use super::evidence::{same_runtime_identity, validate_exact_tests, validate_qualification_report};
use super::handoff::{materialize_tag_source, materialize_tag_source_to};
use crate::model::{
    Architecture, BuildKey, Check, NativeReport, NativeStatus, NativeStep, Product, SourceIdentity,
    required_runtime_tests,
};
use crate::process::SystemRunner;
use crate::qualification::{
    QualificationMode, QualificationReport, RuntimePairOrigin, VerifiedRuntimeArchive,
    VerifiedRuntimePair,
};
use anyhow::{Result, ensure};
use std::path::{Path, PathBuf};
use std::process::Command;

#[test]
fn extracts_only_static_tag_data_without_git_metadata() -> Result<()> {
    let workspace = tempfile::tempdir()?;
    let repository = workspace.path().join("repository");
    std::fs::create_dir(&repository)?;
    let git = git_executable()?;
    setup_git(&git, &repository, &["init"])?;
    setup_git(
        &git,
        &repository,
        &["config", "user.name", "Tag Source Test"],
    )?;
    setup_git(
        &git,
        &repository,
        &["config", "user.email", "tag-source@example.invalid"],
    )?;
    std::fs::write(
        repository.join("CMakeLists.txt"),
        "project(InputNextAdapters VERSION 1.2.3 LANGUAGES CXX)\n",
    )?;
    std::fs::create_dir_all(repository.join("third_party/inputnext/include"))?;
    std::fs::write(
        repository.join("third_party/inputnext/include/input_next_core.h"),
        b"tagged SDK bytes\n",
    )?;
    setup_git(&git, &repository, &["add", "."])?;
    setup_git(&git, &repository, &["commit", "-m", "tag data"])?;
    let commit = setup_git(&git, &repository, &["rev-parse", "HEAD"])?;

    let mut runner = SystemRunner;
    let source = materialize_tag_source(&repository, &git, &commit, &mut runner)?;
    assert_eq!(
        std::fs::read(
            source
                .root()
                .join("third_party/inputnext/include/input_next_core.h")
        )?,
        b"tagged SDK bytes\n"
    );
    assert!(!source.root().join(".git").exists());
    let persistent = workspace.path().join("tag-source-output");
    let output = materialize_tag_source_to(&repository, &git, &commit, &persistent, &mut runner)?;
    assert_eq!(output, dunce::canonicalize(&persistent)?);
    assert_eq!(
        std::fs::read(output.join("third_party/inputnext/include/input_next_core.h"))?,
        b"tagged SDK bytes\n"
    );
    assert!(!output.join(".git").exists());
    assert!(materialize_tag_source_to(&repository, &git, &commit, &output, &mut runner).is_err());
    Ok(())
}

#[test]
fn qualification_admission_uses_portable_runtime_identity_and_exact_tests() -> Result<()> {
    let product = Product::SkyrimSe;
    let architecture = Architecture::X64;
    let expected = required_runtime_tests(product)
        .iter()
        .map(|test| (*test).to_owned())
        .collect::<Vec<_>>();
    let source = SourceIdentity {
        commit: Some("a".repeat(40)),
        modified: false,
        snapshot_sha256: None,
    };
    let trusted_runtime = runtime_pair("/final-job/runtime");
    let producer_runtime = runtime_pair("/producer-job/runtime");
    assert!(same_runtime_identity(&trusted_runtime, &producer_runtime));

    let native_report = NativeReport {
        schema_version: 1,
        key: BuildKey {
            product,
            architecture,
            check: Check::Tests,
        },
        source: source.clone(),
        status: NativeStatus::Passed,
        required_tests: expected.clone(),
        discovered_tests: expected.clone(),
        executed_tests: expected.clone(),
        steps: vec![NativeStep {
            name: "runtime package tests".to_owned(),
            status: NativeStatus::Passed,
            exit_code: Some(0),
            stdout_log: "/producer-job/logs/stdout.txt".into(),
            stderr_log: "/producer-job/logs/stderr.txt".into(),
            diagnostic: None,
        }],
        junit_path: Some("/producer-job/junit/report.xml".into()),
        sarif_paths: Vec::new(),
        diagnostic_paths: Vec::new(),
        failure: None,
    };
    let report = QualificationReport {
        schema_version: 1,
        product,
        architecture,
        mode: QualificationMode::Release,
        source: source.clone(),
        runtime_pair: producer_runtime,
        required_tests: expected.clone(),
        discovered_tests: expected.clone(),
        executed_tests: expected,
        native_report,
        release_eligible: false,
    };

    validate_qualification_report(&report, product, architecture, &source, &trusted_runtime)?;
    assert!(
        validate_exact_tests(
            &["required".to_owned()],
            &["required".to_owned(), "extra".to_owned()],
            &["required".to_owned()],
            "fixture",
        )
        .is_err()
    );
    assert!(
        validate_exact_tests(
            &["required".to_owned(), "required".to_owned()],
            &["required".to_owned()],
            &["required".to_owned()],
            "fixture",
        )
        .is_err()
    );
    Ok(())
}

fn runtime_pair(root: &str) -> VerifiedRuntimePair {
    VerifiedRuntimePair {
        schema_version: 1,
        origin: RuntimePairOrigin::Published,
        repository: Some("example/runtime".to_owned()),
        release_id: Some(42),
        tag: Some("runtime/v1.2.3".to_owned()),
        version: "1.2.3".to_owned(),
        core_sha256: "1".repeat(64),
        effects_sha256: "2".repeat(64),
        manifest_sha256: "3".repeat(64),
        x64: VerifiedRuntimeArchive {
            architecture: Architecture::X64,
            path: PathBuf::from(root).join("x64.zip"),
            sha256: "4".repeat(64),
            size_bytes: 4096,
            asset_id: Some(100),
            asset_name: Some("runtime-x64.zip".to_owned()),
            bin_directory: PathBuf::from(root).join("x64/bin"),
        },
        x86: VerifiedRuntimeArchive {
            architecture: Architecture::X86,
            path: PathBuf::from(root).join("x86.zip"),
            sha256: "5".repeat(64),
            size_bytes: 2048,
            asset_id: Some(101),
            asset_name: Some("runtime-x86.zip".to_owned()),
            bin_directory: PathBuf::from(root).join("x86/bin"),
        },
    }
}

fn git_executable() -> Result<PathBuf> {
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
    anyhow::bail!("Git executable is required for isolated release tests")
}

fn setup_git(git: &Path, root: &Path, args: &[&str]) -> Result<String> {
    let output = Command::new(git)
        .args(args)
        .current_dir(root)
        .env("GIT_CONFIG_NOSYSTEM", "1")
        .env("GIT_CONFIG_GLOBAL", root.join("empty-global-config"))
        .output()?;
    ensure!(
        output.status.success(),
        "isolated Git fixture setup failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(String::from_utf8(output.stdout)?.trim().to_owned())
}
