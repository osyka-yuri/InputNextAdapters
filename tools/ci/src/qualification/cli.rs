use std::fs;
use std::num::NonZeroUsize;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};

use crate::model::{ExecutionContext, NativeOptions, NativeStatus, SourceIdentity};
use crate::plan::{build_key_slug, create_plan};
use crate::process::Runner;
use crate::source::identify_source;
use crate::tooling::{load_toolset, write_json_report};

use super::{QualificationMode, RuntimeInputValidationReport, execute, input};

const MAX_JOBS: usize = 256;
const MAX_NATIVE_REPORT_BYTES: u64 = 16 * 1024 * 1024;

pub(super) fn run(args: &super::QualifyArgs, runner: &mut dyn Runner) -> Result<()> {
    ensure!(
        args.output.is_absolute(),
        "qualification output must be absolute"
    );
    ensure!(
        args.report.is_absolute(),
        "qualification report must be absolute"
    );
    ensure!(
        args.manifest.is_absolute(),
        "runtime manifest path must be absolute"
    );

    let root = dunce::canonicalize(&args.root).with_context(|| {
        format!(
            "could not resolve qualification root {}",
            args.root.display()
        )
    })?;
    ensure!(root.is_dir(), "qualification root is not a directory");
    let output = create_output_directory(&args.output)?;
    let report_path = validate_report_path(&output, &args.report)?;
    let toolset = load_toolset(&args.toolset)?;
    let jobs = args.jobs.unwrap_or_else(default_jobs);
    ensure!(
        jobs.get() <= MAX_JOBS,
        "jobs must be between 1 and {MAX_JOBS}"
    );

    if args.validate_input {
        return validate_input(args, &root, output, &report_path, jobs, toolset, runner);
    }

    run_native_qualification(args, &root, &output, &report_path, jobs, toolset, runner)
}

fn validate_input(
    args: &super::QualifyArgs,
    root: &Path,
    output: PathBuf,
    report_path: &Path,
    jobs: NonZeroUsize,
    toolset: crate::tooling::Toolset,
    runner: &mut dyn Runner,
) -> Result<()> {
    let context = ExecutionContext {
        repository: root.to_path_buf(),
        output,
        jobs,
        toolset,
        compiler_report: None,
        source: SourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: None,
        },
    };
    let pair = input::inspect_runtime_pair(&args.manifest, root, &context, args.mode, runner)?;
    let report = RuntimeInputValidationReport {
        schema_version: 1,
        status: "input-validated".to_owned(),
        runtime_pair: pair,
    };
    write_json_report(report_path, &report)?;
    println!("Runtime-pair input statically validated; no native tests were run");
    Ok(())
}

fn run_native_qualification(
    args: &super::QualifyArgs,
    root: &Path,
    output: &Path,
    report_path: &Path,
    jobs: NonZeroUsize,
    toolset: crate::tooling::Toolset,
    runner: &mut dyn Runner,
) -> Result<()> {
    let source = identify_source(root, runner)?;
    if args.mode == QualificationMode::Release {
        ensure!(
            execute::is_clean_release_source(&source),
            "release qualification requires a clean committed source snapshot"
        );
    }
    let context = ExecutionContext {
        repository: root.to_path_buf(),
        output: output.to_path_buf(),
        jobs,
        toolset,
        compiler_report: None,
        source,
    };
    let pair = input::inspect_runtime_pair(&args.manifest, root, &context, args.mode, runner)?;
    let runtime = match args.architecture {
        crate::model::Architecture::X64 => &pair.x64,
        crate::model::Architecture::X86 => &pair.x86,
    };
    let options = NativeOptions {
        runtime_directory: Some(runtime.bin_directory.clone()),
        release_build: args.mode == QualificationMode::Release,
        ..NativeOptions::default()
    };
    let plan = create_plan(
        root,
        output,
        args.product,
        args.architecture,
        crate::model::Check::Tests,
        &options,
    )?;
    let required_tests = plan.required_tests.clone();
    let native_path = output
        .join("reports")
        .join(format!("{}.json", build_key_slug(plan.key)));
    ensure_report_absent(&native_path)?;

    let report = match super::qualify_runtime_pair(
        &pair,
        &plan,
        &context,
        &required_tests,
        &options,
        runner,
    ) {
        Ok(report) => report,
        Err(error) => {
            let Ok(failed) = read_native_report(&native_path) else {
                return Err(error);
            };
            ensure!(
                failed.status == NativeStatus::Failed,
                "native command failed without a failed native report"
            );
            let qualification = execute::qualification_report(
                args.product,
                args.architecture,
                args.mode,
                context.source.clone(),
                pair.clone(),
                required_tests,
                failed,
            )?;
            write_json_report(report_path, &qualification)?;
            return Err(error).context(format!("qualification report: {}", report_path.display()));
        }
    };
    write_json_report(&output.join("native-report.json"), &report.native_report)?;
    write_json_report(report_path, &report)?;
    ensure!(
        args.mode != QualificationMode::Release || report.release_eligible,
        "release qualification passed its cold tests but is not release-eligible"
    );
    println!(
        "{} qualification for {} {} ({})",
        if report.release_eligible {
            "Release-ready"
        } else {
            "Passed"
        },
        report.product.as_str(),
        report.architecture.as_str(),
        report.runtime_pair.version
    );
    Ok(())
}

fn create_output_directory(path: &Path) -> Result<PathBuf> {
    fs::create_dir_all(path)
        .with_context(|| format!("could not create qualification output {}", path.display()))?;
    let metadata = fs::symlink_metadata(path).context("could not inspect qualification output")?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "qualification output must be a regular directory"
    );
    dunce::canonicalize(path).context("could not resolve qualification output")
}

fn validate_report_path(output: &Path, report: &Path) -> Result<PathBuf> {
    let parent = report
        .parent()
        .context("qualification report has no parent")?;
    let parent =
        dunce::canonicalize(parent).context("could not resolve qualification report directory")?;
    ensure!(
        parent == output,
        "qualification report must be directly inside --output"
    );
    ensure!(
        report
            .file_name()
            .is_some_and(|name| name == "qualification-report.json"),
        "qualification report filename must be qualification-report.json"
    );
    if let Ok(metadata) = fs::symlink_metadata(report) {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "qualification report destination must be a regular file"
        );
    }
    Ok(parent.join("qualification-report.json"))
}

fn ensure_report_absent(path: &Path) -> Result<()> {
    match fs::symlink_metadata(path) {
        Ok(_) => anyhow::bail!(
            "native qualification report already exists and cannot be treated as current evidence"
        ),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error).context("could not inspect native qualification report"),
    }
}

fn read_native_report(path: &Path) -> Result<crate::model::NativeReport> {
    let metadata = fs::symlink_metadata(path).with_context(|| {
        format!(
            "native check failed before creating report {}",
            path.display()
        )
    })?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= MAX_NATIVE_REPORT_BYTES,
        "native report must be a bounded regular file"
    );
    crate::json::parse(&fs::read(path)?).context("native failure report is malformed")
}

fn default_jobs() -> NonZeroUsize {
    let available = std::thread::available_parallelism().unwrap_or(NonZeroUsize::MIN);
    available.min(NonZeroUsize::new(16).unwrap_or(NonZeroUsize::MIN))
}
