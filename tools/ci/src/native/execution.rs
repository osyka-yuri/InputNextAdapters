use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};

use crate::model::{
    BuildKey, BuildPlan, Check, ExecutionContext, NativeOptions, NativeReport, NativeStatus,
    NativeStep, Product,
};
use crate::process::{CommandSpec, Runner};
use crate::tooling::Toolset;

use super::{command_environment, configure_arguments, junit};

const NATIVE_TIMEOUT: Duration = Duration::from_hours(2);
const NATIVE_OUTPUT_LIMIT: usize = 64 * 1024 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TestScope {
    All,
    Required,
}

pub(crate) fn test_scope(check: Check, options: &NativeOptions) -> TestScope {
    if check == Check::Package || (check == Check::Tests && options.runtime_directory.is_some()) {
        TestScope::Required
    } else {
        TestScope::All
    }
}

#[allow(
    clippy::too_many_arguments,
    reason = "the execution boundary keeps plans and trust inputs explicit"
)]
pub(crate) fn execute_plan(
    plan: &BuildPlan,
    source_directory: &Path,
    build_directory: &Path,
    log_directory: &Path,
    junit_path: &Path,
    context: &ExecutionContext,
    options: &NativeOptions,
    required_tests: &[String],
    runner: &mut dyn Runner,
    report: &mut NativeReport,
) -> Result<()> {
    let cmake = require_tool(&context.toolset, "cmake")?;
    let ninja = require_tool(&context.toolset, "ninja")?;
    let ctest = require_sibling_tool(&cmake, if cfg!(windows) { "ctest.exe" } else { "ctest" })?;
    let environment = command_environment(&context.toolset)?;

    if needs_repository_quality_check(plan.key.check) {
        let powershell = require_tool(&context.toolset, "powershell")?;
        let quality_script = require_regular_file(
            &context.repository.join("tools/check_quality.ps1"),
            "repository quality script",
        )?;
        let quality_args = vec![
            "-NoLogo".into(),
            "-NoProfile".into(),
            "-NonInteractive".into(),
            "-File".into(),
            quality_script.into_os_string(),
        ];
        run_step(
            runner,
            report,
            log_directory,
            "repository-quality",
            powershell,
            quality_args,
            &context.repository,
            environment.clone(),
            false,
        )?;
    }

    let configure_args = configure_arguments(plan, build_directory, options, &context.toolset)?;
    run_step(
        runner,
        report,
        log_directory,
        "configure",
        cmake.clone(),
        configure_args,
        source_directory,
        environment.clone(),
        false,
    )?;

    let build_args = vec![
        "--build".into(),
        build_directory.as_os_str().to_owned(),
        "--parallel".into(),
        context.jobs.to_string().into(),
    ];
    run_step(
        runner,
        report,
        log_directory,
        "build",
        cmake,
        build_args,
        source_directory,
        environment.clone(),
        false,
    )?;

    if should_run_tests(plan.key, required_tests) {
        run_test_suite(
            source_directory,
            build_directory,
            log_directory,
            junit_path,
            context,
            required_tests,
            test_scope(plan.key.check, options),
            "",
            ctest,
            &environment,
            runner,
            report,
        )?;
    }
    let _ = ninja;
    Ok(())
}
#[allow(
    clippy::too_many_arguments,
    reason = "test evidence stays bound to its plan, paths, and runner"
)]
pub(crate) fn run_test_suite(
    source_directory: &Path,
    build_directory: &Path,
    log_directory: &Path,
    junit_path: &Path,
    context: &ExecutionContext,
    required_tests: &[String],
    scope: TestScope,
    step_prefix: &str,
    ctest: PathBuf,
    environment: &BTreeMap<OsString, OsString>,
    runner: &mut dyn Runner,
    report: &mut NativeReport,
) -> Result<()> {
    ensure!(
        step_prefix.is_empty()
            || step_prefix
                .bytes()
                .all(|byte| byte.is_ascii_lowercase() || byte == b'-'),
        "CTest step prefix is invalid"
    );
    validate_required_tests(required_tests)?;
    let test_filter = match scope {
        TestScope::All => Vec::new(),
        TestScope::Required => ctest_filter_arguments(required_tests)?,
    };
    let discovery_name = format!("{step_prefix}discover-tests");
    let mut discovery_args = vec![
        "--test-dir".into(),
        build_directory.as_os_str().to_owned(),
        "--show-only=json-v1".into(),
    ];
    discovery_args.extend(test_filter.iter().cloned());
    let discovery = run_step(
        runner,
        report,
        log_directory,
        &discovery_name,
        ctest.clone(),
        discovery_args,
        source_directory,
        environment.clone(),
        false,
    )?;
    let listing = junit::parse_ctest_listing(&fs::read(&discovery.stdout_log)?)?;
    report.discovered_tests.clone_from(&listing.names);
    validate_required_discovery(required_tests, &listing)?;
    ensure!(
        !listing.names.is_empty(),
        "native test plan discovered zero CTest cases"
    );

    let test_name = format!("{step_prefix}ctest");
    prepare_junit_destination(junit_path)?;
    let test_step = run_step(
        runner,
        report,
        log_directory,
        &test_name,
        ctest,
        {
            let mut args = vec![
                "--test-dir".into(),
                build_directory.as_os_str().to_owned(),
                "--output-on-failure".into(),
                "--no-tests=error".into(),
                "--parallel".into(),
                context.jobs.to_string().into(),
                "--output-junit".into(),
                junit_path.as_os_str().to_owned(),
            ];
            args.extend(test_filter);
            args
        },
        source_directory,
        environment.clone(),
        true,
    )?;
    report.junit_path = Some(junit_path.to_path_buf());
    report.diagnostic_paths.push(junit_path.to_path_buf());
    let cases = junit::parse_junit(&read_bounded_junit(junit_path)?)?;
    report.executed_tests = cases.iter().map(|case| case.name.clone()).collect();
    validate_test_results(required_tests, &listing, &cases)?;
    ensure!(
        test_step.status == NativeStatus::Passed,
        "CTest exited unsuccessfully; details are in the native step logs"
    );
    Ok(())
}

#[allow(
    clippy::too_many_arguments,
    reason = "command logging is kept at the process boundary"
)]
pub(crate) fn run_step(
    runner: &mut dyn Runner,
    report: &mut NativeReport,
    log_directory: &Path,
    name: &str,
    program: PathBuf,
    args: Vec<OsString>,
    cwd: &Path,
    env: BTreeMap<OsString, OsString>,
    allow_nonzero: bool,
) -> Result<NativeStep> {
    let stdout_log = log_directory.join(format!("{name}.stdout.log"));
    let stderr_log = log_directory.join(format!("{name}.stderr.log"));
    let output = runner.run(&CommandSpec {
        program,
        args,
        cwd: cwd.to_path_buf(),
        env,
        timeout: NATIVE_TIMEOUT,
        output_limit: NATIVE_OUTPUT_LIMIT,
    });
    let (exit_code, diagnostic, status) = match output {
        Ok(output) => {
            write_regular_file(&stdout_log, &output.stdout)?;
            write_regular_file(&stderr_log, &output.stderr)?;
            let code = output.code;
            let success = code == Some(0);
            let detail = (!success).then(|| output.diagnostics());
            (
                code,
                detail,
                if success {
                    NativeStatus::Passed
                } else {
                    NativeStatus::Failed
                },
            )
        }
        Err(error) => {
            let details = format!("{error:#}");
            write_regular_file(&stdout_log, b"")?;
            write_regular_file(&stderr_log, details.as_bytes())?;
            (None, Some(details), NativeStatus::Failed)
        }
    };
    report
        .diagnostic_paths
        .extend([stdout_log.clone(), stderr_log.clone()]);
    let step = NativeStep {
        name: name.to_owned(),
        status,
        exit_code,
        stdout_log,
        stderr_log,
        diagnostic,
    };
    report.steps.push(step.clone());
    if step.status == NativeStatus::Failed && !allow_nonzero {
        bail!(
            "native step '{name}' failed with exit status {:?}; stderr log: {}",
            step.exit_code,
            step.stderr_log.display()
        );
    }
    Ok(step)
}

fn validate_required_discovery(required: &[String], listing: &junit::CTestListing) -> Result<()> {
    for test in required {
        ensure!(
            listing.names.contains(test),
            "required CTest case '{test}' was not discovered"
        );
        ensure!(
            !listing.disabled.contains(test),
            "required CTest case '{test}' is disabled"
        );
    }
    Ok(())
}

fn validate_test_results(
    required: &[String],
    listing: &junit::CTestListing,
    cases: &[junit::JUnitCase],
) -> Result<()> {
    let executed = cases
        .iter()
        .map(|case| case.name.as_str())
        .collect::<BTreeSet<_>>();
    ensure!(
        !cases.is_empty(),
        "CTest JUnit report recorded zero executed tests"
    );
    for name in &listing.names {
        if listing.disabled.contains(name) {
            continue;
        }
        ensure!(
            executed.contains(name.as_str()),
            "discovered CTest case '{name}' did not appear in the JUnit execution report"
        );
    }
    for case in cases {
        ensure!(!case.skipped, "CTest case '{}' was skipped", case.name);
        ensure!(!case.failed, "CTest case '{}' failed", case.name);
    }
    for test in required {
        ensure!(
            executed.contains(test.as_str()),
            "required CTest case '{test}' was not executed"
        );
    }
    Ok(())
}

pub(crate) fn ctest_filter_arguments(required: &[String]) -> Result<Vec<OsString>> {
    if required.is_empty() {
        return Ok(Vec::new());
    }

    let pattern = format!(
        "^({})$",
        required
            .iter()
            .map(|name| name.replace('.', r"\."))
            .collect::<Vec<_>>()
            .join("|")
    );
    ensure!(
        pattern.len() <= 16 * 1024,
        "required CTest filter exceeds the supported command-line size"
    );
    Ok(vec!["-R".into(), pattern.into()])
}

fn should_run_tests(key: BuildKey, required_tests: &[String]) -> bool {
    !required_tests.is_empty()
        || !matches!(key.check, Check::Package)
        || key.product != Product::Foundation
}

pub(crate) fn needs_repository_quality_check(check: Check) -> bool {
    matches!(check, Check::Quality | Check::NativeQuality)
}

pub(crate) fn validate_required_tests(required: &[String]) -> Result<()> {
    ensure!(required.len() <= 256, "too many required CTest names");
    let mut seen = BTreeSet::new();
    for name in required {
        ensure!(!name.is_empty(), "required CTest name is empty");
        ensure!(
            name.len() <= 512
                && name
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-')),
            "required CTest name contains unsupported characters"
        );
        ensure!(
            seen.insert(name),
            "required CTest name '{name}' is repeated"
        );
    }
    Ok(())
}

pub(crate) fn validate_typed_paths(product: Product, options: &NativeOptions) -> Result<()> {
    ensure!(
        !(options.source_bundle.is_some() && options.release_build),
        "release builds cannot consume a source bundle"
    );
    if options.source_bundle.is_some() {
        ensure!(
            product != Product::Foundation,
            "foundation plans do not accept adapter source bundles"
        );
    }
    if let Some(runtime) = &options.runtime_directory {
        let _ = canonical_directory(runtime, "runtime package bin directory")?;
    }
    if let Some(plugin) = &options.package_plugin {
        let _ = require_regular_file(plugin, "package plugin")?;
    }
    if let Some(bundle) = &options.source_bundle {
        let _ = require_regular_file(bundle, "source bundle manifest")?;
    }
    for path in [
        options.runtime_directory.as_deref(),
        options.package_plugin.as_deref(),
        options.source_bundle.as_deref(),
    ]
    .into_iter()
    .flatten()
    {
        ensure!(
            !path.to_string_lossy().contains(';'),
            "native CMake paths must not contain semicolons"
        );
    }
    Ok(())
}

pub(crate) fn cmake_path_argument(prefix: &str, path: &Path) -> Result<OsString> {
    ensure!(
        !path.to_string_lossy().contains(';'),
        "native CMake paths must not contain semicolons"
    );
    let mut argument = OsString::from(prefix);
    argument.push(path.as_os_str());
    Ok(argument)
}

pub(crate) fn validate_host() -> Result<()> {
    ensure!(
        cfg!(windows) && env::consts::ARCH == "x86_64",
        "native adapter verification requires a Windows x64 host"
    );
    Ok(())
}

pub(crate) fn reject_release_source_overrides() -> Result<()> {
    for (name, _) in env::vars_os() {
        let name = name.to_string_lossy();
        let uppercase = name.to_ascii_uppercase();
        ensure!(
            uppercase != "INPUTNEXTADAPTERS_SOURCE_BUNDLE"
                && !uppercase.starts_with("FETCHCONTENT_SOURCE_DIR_"),
            "release builds reject source override environment variable '{name}'"
        );
    }
    Ok(())
}

pub(crate) fn ensure_fresh_build_directory(path: &Path, output: &Path) -> Result<PathBuf> {
    if let Ok(metadata) = fs::symlink_metadata(path) {
        ensure!(
            metadata.is_dir() && !metadata.file_type().is_symlink(),
            "release build directory must be a regular directory"
        );
        ensure!(
            fs::read_dir(path)?.next().is_none(),
            "release builds require a fresh empty build directory"
        );
    }
    let resolved = create_safe_directory(path)?;
    ensure!(
        resolved.starts_with(output),
        "release build directory escaped the output root"
    );
    Ok(resolved)
}

pub(crate) fn require_tool(toolset: &Toolset, name: &str) -> Result<PathBuf> {
    let path = toolset.executable(name)?.to_path_buf();
    require_regular_file(&path, &format!("verified {name} tool"))
}

pub(crate) fn require_sibling_tool(cmake: &Path, name: &str) -> Result<PathBuf> {
    let parent = cmake
        .parent()
        .context("CMake executable has no parent directory")?;
    require_regular_file(&parent.join(name), name)
}

pub(crate) fn require_regular_file(path: &Path, description: &str) -> Result<PathBuf> {
    ensure!(path.is_absolute(), "{description} path must be absolute");
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect {description} {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "{description} must be a regular non-symlink file"
    );
    dunce::canonicalize(path)
        .with_context(|| format!("could not resolve {description} {}", path.display()))
}

pub(crate) fn canonical_directory(path: &Path, description: &str) -> Result<PathBuf> {
    ensure!(path.is_absolute(), "{description} path must be absolute");
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect {description} {}", path.display()))?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "{description} must be a regular non-symlink directory"
    );
    let directory = dunce::canonicalize(path)
        .with_context(|| format!("could not resolve {description} {}", path.display()))?;
    let runtime_dll = directory.join("inputnext_runtime.dll");
    let runtime_dll_metadata = fs::symlink_metadata(&runtime_dll).with_context(|| {
        format!(
            "runtime package bin directory is missing {}",
            runtime_dll.display()
        )
    })?;
    ensure!(
        runtime_dll_metadata.is_file() && !runtime_dll_metadata.file_type().is_symlink(),
        "runtime DLL must be a regular non-symlink file"
    );
    Ok(directory)
}

pub(crate) fn create_safe_directory(path: &Path) -> Result<PathBuf> {
    ensure!(
        path.is_absolute(),
        "native output directory must be absolute"
    );
    let mut current = PathBuf::new();
    for component in path.components() {
        match component {
            Component::Prefix(prefix) => current.push(prefix.as_os_str()),
            Component::RootDir => current.push(component.as_os_str()),
            Component::CurDir => {}
            Component::ParentDir => bail!("native output path is not normalized"),
            Component::Normal(name) => {
                current.push(name);
                match fs::symlink_metadata(&current) {
                    Ok(metadata) => ensure!(
                        metadata.is_dir() && !metadata.file_type().is_symlink(),
                        "native output path component is not a regular directory: {}",
                        current.display()
                    ),
                    Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                        fs::create_dir(&current).with_context(|| {
                            format!(
                                "could not create native output directory {}",
                                current.display()
                            )
                        })?;
                    }
                    Err(error) => {
                        return Err(error).with_context(|| {
                            format!("could not inspect native output {}", current.display())
                        });
                    }
                }
            }
        }
    }
    dunce::canonicalize(path)
        .with_context(|| format!("could not resolve native output {}", path.display()))
}

pub(crate) fn create_child_directory(root: &Path, relative: PathBuf) -> Result<PathBuf> {
    ensure!(
        relative
            .components()
            .all(|component| matches!(component, Component::Normal(_))),
        "native output child path is unsafe"
    );
    let directory = create_safe_directory(&root.join(relative))?;
    ensure!(
        directory.starts_with(root),
        "native output directory escaped output root"
    );
    Ok(directory)
}

fn write_regular_file(path: &Path, contents: &[u8]) -> Result<()> {
    if let Ok(metadata) = fs::symlink_metadata(path) {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "native log destination must be a regular non-symlink file"
        );
    }
    let parent = path.parent().context("native log path has no parent")?;
    let mut file = tempfile::NamedTempFile::new_in(parent).with_context(|| {
        format!(
            "could not create temporary native log in {}",
            parent.display()
        )
    })?;
    file.write_all(contents)
        .with_context(|| format!("could not finish native log {}", path.display()))?;
    file.flush()
        .with_context(|| format!("could not flush native log {}", path.display()))?;
    file.persist(path)
        .with_context(|| format!("could not publish native log {}", path.display()))?;
    Ok(())
}

fn prepare_junit_destination(path: &Path) -> Result<()> {
    if let Ok(metadata) = fs::symlink_metadata(path) {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "JUnit destination must be a regular non-symlink file"
        );
        fs::remove_file(path)
            .with_context(|| format!("could not replace JUnit report {}", path.display()))?;
    }
    OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path)
        .with_context(|| format!("could not prepare JUnit report {}", path.display()))?;
    Ok(())
}

fn read_bounded_junit(path: &Path) -> Result<Vec<u8>> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("CTest did not create JUnit report {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "CTest JUnit report must be a regular non-symlink file"
    );
    ensure!(
        metadata.len() <= 64 * 1024 * 1024,
        "CTest JUnit report exceeds the size limit"
    );
    fs::read(path).with_context(|| format!("could not read CTest JUnit report {}", path.display()))
}
