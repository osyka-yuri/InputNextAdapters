//! Fixed `CMake`/`CTest` orchestration for supported product plans.

mod execution;
mod junit;
mod package_probe;
pub use package_probe::run_package_probe;

use std::ffi::OsString;
use std::num::NonZeroUsize;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};
use clap::Args;
use serde::Serialize;

use crate::model::{
    Architecture, BuildKey, BuildPlan, Check, ExecutionContext, NativeOptions, NativeReport,
    NativeStatus, NativeStep, Product, SourceIdentity,
};
use crate::plan::{build_key_slug, create_plan, resolve_output};
use crate::process::Runner;
use crate::source::identify_source;
use crate::tooling::{Toolset, load_toolset, write_json_report};

const MAX_JOBS: usize = 256;

struct NativePaths {
    source_directory: PathBuf,
    build_directory: PathBuf,
    log_directory: PathBuf,
    report_path: PathBuf,
    junit_path: PathBuf,
}

pub(crate) use crate::tooling::command_environment;
pub(crate) use execution::{
    canonical_directory, cmake_path_argument, create_child_directory, create_safe_directory,
    ensure_fresh_build_directory, execute_plan, reject_release_source_overrides,
    require_regular_file, require_sibling_tool, require_tool, run_step, run_test_suite,
    validate_host, validate_required_tests, validate_typed_paths,
};
#[cfg(test)]
pub(crate) use execution::{ctest_filter_arguments, needs_repository_quality_check};

#[derive(Debug, Args)]
pub struct NativeArgs {
    #[arg(long, default_value = ".")]
    root: PathBuf,
    #[arg(long, default_value = "build/ci")]
    output: PathBuf,
    /// Optional explicit report path; it must be the stable path beneath output/reports.
    #[arg(long)]
    report: Option<PathBuf>,
    #[arg(long, value_enum)]
    product: Product,
    #[arg(long, value_enum)]
    architecture: Architecture,
    #[arg(long, value_enum)]
    check: Check,
    #[arg(long)]
    jobs: Option<NonZeroUsize>,
    #[arg(long)]
    toolset: PathBuf,
    #[arg(long)]
    compiler_report: Option<PathBuf>,
    #[arg(long)]
    runtime_directory: Option<PathBuf>,
    #[arg(long)]
    package_plugin: Option<PathBuf>,
    #[arg(long)]
    source_bundle: Option<PathBuf>,
    #[arg(long)]
    release_build: bool,
}

impl NativeArgs {
    /// Executes one fixed native plan and records test and compiler provenance.
    ///
    /// # Errors
    ///
    /// Returns an error if plan admission, tool loading, source identity, or native execution fails.
    pub fn run(self, runner: &mut dyn Runner) -> Result<()> {
        validate_host()?;
        if let Some(jobs) = self.jobs {
            ensure!(
                jobs.get() <= MAX_JOBS,
                "jobs must be between 1 and {MAX_JOBS}"
            );
        }
        let repository = dunce::canonicalize(&self.root).with_context(|| {
            format!("could not resolve repository root {}", self.root.display())
        })?;
        ensure!(repository.is_dir(), "repository root is not a directory");
        let output = resolve_output(&repository, &self.output)?;
        let options = NativeOptions {
            runtime_directory: self.runtime_directory,
            package_plugin: self.package_plugin,
            source_bundle: self.source_bundle,
            release_build: self.release_build,
        };
        let plan = create_plan(
            &repository,
            &output,
            self.product,
            self.architecture,
            self.check,
            &options,
        )?;
        let report_path = output
            .join("reports")
            .join(format!("{}.json", build_key_slug(plan.key)));
        if let Some(explicit_path) = &self.report {
            let requested = resolve_output(&repository, explicit_path)?;
            ensure!(
                requested == report_path,
                "report path must be the stable report destination {}",
                report_path.display()
            );
        }
        let toolset = load_toolset(&self.toolset)?;
        let source = identify_source(&repository, runner)?;
        let context = ExecutionContext {
            repository,
            output,
            jobs: self.jobs.unwrap_or_else(default_jobs),
            toolset,
            compiler_report: self.compiler_report,
            source,
        };
        let report = run_native(&plan, &context, &plan.required_tests, &options, runner)?;
        println!(
            "Native {} check passed for {} {} with {} test(s)",
            report.key.check.as_str(),
            report.key.product.as_str(),
            report.key.architecture.as_str(),
            report.executed_tests.len()
        );
        Ok(())
    }
}

/// Executes one supported plan and writes a typed success or failure report.
///
/// All commands, `CMake` cache keys, and output paths are selected by this module. Callers can
/// supply exact required `CTest` names, but cannot supply arbitrary commands or `CMake` arguments.
///
/// # Errors
///
/// Returns an error when validation, process execution, required-test admission, or report
/// creation fails. Once execution starts, the report records process and test failure details.
pub fn run_native(
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
    runner: &mut dyn Runner,
) -> Result<NativeReport> {
    let paths = prepare_native_paths(plan, context, required_tests, options)?;
    let mut report = NativeReport {
        schema_version: 1,
        key: plan.key,
        source: context.source.clone(),
        status: NativeStatus::Failed,
        required_tests: required_tests.to_vec(),
        discovered_tests: Vec::new(),
        executed_tests: Vec::new(),
        steps: Vec::new(),
        junit_path: None,
        sarif_paths: Vec::new(),
        diagnostic_paths: Vec::new(),
        failure: None,
    };
    if let Some(compiler_report) = &context.compiler_report {
        let path = require_regular_file(compiler_report, "compiler report")?;
        report.diagnostic_paths.push(path);
    }

    let execution = execute_plan(
        plan,
        &paths.source_directory,
        &paths.build_directory,
        &paths.log_directory,
        &paths.junit_path,
        context,
        options,
        required_tests,
        runner,
        &mut report,
    );
    let status = match execution {
        Ok(()) => {
            report.status = NativeStatus::Passed;
            Ok(())
        }
        Err(error) => {
            report.status = NativeStatus::Failed;
            report.failure = Some(format!("{error:#}"));
            Err(error)
        }
    };
    write_json_report(&paths.report_path, &report).with_context(|| {
        format!(
            "could not write native report {}",
            paths.report_path.display()
        )
    })?;
    status.with_context(|| {
        format!(
            "native check failed; report: {}",
            paths.report_path.display()
        )
    })?;
    Ok(report)
}

fn prepare_native_paths(
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
) -> Result<NativePaths> {
    validate_host()?;
    ensure!(
        context.jobs.get() <= MAX_JOBS,
        "jobs must be between 1 and {MAX_JOBS}"
    );
    ensure!(
        context.toolset.platform() == "windows-x64",
        "native adapter verification requires a Windows x64 toolset"
    );
    validate_required_tests(required_tests)?;
    if options.release_build {
        reject_release_source_overrides()?;
    }

    let repository = dunce::canonicalize(&context.repository).with_context(|| {
        format!(
            "could not resolve repository {}",
            context.repository.display()
        )
    })?;
    let output = create_safe_directory(&context.output)?;
    let expected_plan = create_plan(
        &repository,
        &output,
        plan.key.product,
        plan.key.architecture,
        plan.key.check,
        options,
    )?;
    ensure!(
        plan.key == expected_plan.key
            && plan.preset == expected_plan.preset
            && plan.source_directory == expected_plan.source_directory
            && plan.build_directory == expected_plan.build_directory,
        "native plan does not match the fixed product/check mapping"
    );
    validate_typed_paths(plan.key.product, options)?;
    let build_directory = if options.release_build {
        ensure_fresh_build_directory(&plan.build_directory, &output)?
    } else {
        create_safe_directory(&plan.build_directory)?
    };
    ensure!(
        build_directory.starts_with(&output),
        "native build directory escaped the output root"
    );
    let source_directory = dunce::canonicalize(&plan.source_directory).with_context(|| {
        format!(
            "could not resolve source directory {}",
            plan.source_directory.display()
        )
    })?;
    ensure!(
        source_directory.is_dir(),
        "native source directory is not a directory"
    );
    ensure!(
        source_directory == plan.source_directory,
        "native source directory must be canonical"
    );

    let key_slug = build_key_slug(plan.key);
    let log_directory = create_child_directory(&output, Path::new("logs").join(&key_slug))?;
    let report_directory = create_child_directory(&output, PathBuf::from("reports"))?;
    let junit_directory = create_child_directory(&output, PathBuf::from("junit"))?;
    Ok(NativePaths {
        source_directory,
        build_directory,
        log_directory,
        report_path: report_directory.join(format!("{key_slug}.json")),
        junit_path: junit_directory.join(format!("{key_slug}.xml")),
    })
}

/// Exact test evidence returned by the static plugin-package probe.
#[derive(Clone, Debug, Serialize)]
#[serde(deny_unknown_fields)]
pub struct NativeTestEvidence {
    pub schema_version: u32,
    pub key: BuildKey,
    pub source: SourceIdentity,
    pub status: NativeStatus,
    pub cleanup_status: NativeStatus,
    pub required_tests: Vec<String>,
    pub discovered_tests: Vec<String>,
    pub executed_tests: Vec<String>,
    pub steps: Vec<NativeStep>,
    pub junit_path: PathBuf,
    pub diagnostic_paths: Vec<PathBuf>,
    pub failure: Option<String>,
}

/// Builds the fixed `CMake` preset arguments and explicitly clears absent optional paths.
///
/// # Errors
///
/// Returns an error for invalid typed paths or unsafe `CMake` cache values.
pub(crate) fn configure_arguments(
    plan: &BuildPlan,
    build_directory: &Path,
    options: &NativeOptions,
    toolset: &Toolset,
) -> Result<Vec<OsString>> {
    validate_typed_paths(plan.key.product, options)?;
    ensure!(
        build_directory.is_absolute(),
        "native build directory must be absolute"
    );
    ensure!(
        !build_directory.to_string_lossy().contains(';'),
        "native build directory must not contain semicolons"
    );
    let mut args = vec![
        "--preset".into(),
        plan.preset.clone().into(),
        "-B".into(),
        build_directory.as_os_str().to_owned(),
        format!(
            "-DINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL={}",
            if options.release_build { "ON" } else { "OFF" }
        )
        .into(),
    ];
    if options.release_build {
        args.push("-DINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON".into());
    }
    if matches!(plan.key.check, Check::Ubsan | Check::ClangTests) {
        let llvm_clang_tidy = toolset.executable("llvm")?;
        let clang_cl = require_regular_file(
            &llvm_clang_tidy.with_file_name(if cfg!(windows) {
                "clang-cl.exe"
            } else {
                "clang-cl"
            }),
            "locked clang-cl compiler",
        )?;
        for variable in ["CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"] {
            let prefix = format!("-D{variable}:FILEPATH=");
            args.push(cmake_path_argument(&prefix, &clang_cl)?);
        }
    }
    for (name, path, kind, description) in [
        (
            "INPUTNEXTADAPTERS_RUNTIME_DIRECTORY",
            options.runtime_directory.as_deref(),
            "PATH",
            "runtime package bin directory",
        ),
        (
            "INPUTNEXTADAPTERS_PACKAGE_PLUGIN",
            options.package_plugin.as_deref(),
            "FILEPATH",
            "package plugin",
        ),
        (
            "INPUTNEXTADAPTERS_SOURCE_BUNDLE",
            options.source_bundle.as_deref(),
            "FILEPATH",
            "source bundle manifest",
        ),
    ] {
        let prefix = format!("-D{name}:{kind}=");
        if let Some(path) = path {
            let path = if kind == "PATH" {
                canonical_directory(path, description)?
            } else {
                require_regular_file(path, description)?
            };
            args.push(cmake_path_argument(&prefix, &path)?);
        } else {
            args.push(prefix.into());
        }
    }
    Ok(args)
}

fn default_jobs() -> NonZeroUsize {
    let available = std::thread::available_parallelism().unwrap_or(NonZeroUsize::MIN);
    available.min(NonZeroUsize::new(16).unwrap_or(NonZeroUsize::MIN))
}

#[cfg(test)]
#[path = "native/tests.rs"]
mod tests;
