use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use crate::model::{
    BuildPlan, ExecutionContext, NativeOptions, NativeReport, NativeStatus,
    required_plugin_package_tests,
};
use crate::process::Runner;
use crate::tooling::write_json_report;
use anyhow::{Context, Result, ensure};

use super::execution::TestScope;
use super::{
    NativeTestEvidence, build_key_slug, command_environment, configure_arguments,
    create_child_directory, create_plan, create_safe_directory, reject_release_source_overrides,
    require_regular_file, require_sibling_tool, require_tool, run_step, run_test_suite,
    validate_host, validate_required_tests, validate_typed_paths,
};

struct PreparedProbe {
    probe_options: NativeOptions,
    source_directory: PathBuf,
    build_directory: PathBuf,
    log_directory: PathBuf,
    report_path: PathBuf,
    junit_path: PathBuf,
}

struct ProbeCommands {
    cmake: PathBuf,
    ctest: PathBuf,
    environment: BTreeMap<OsString, OsString>,
    configure_arguments: Vec<OsString>,
    restore_arguments: Vec<OsString>,
}

struct ProbeOutcome {
    cleanup_status: NativeStatus,
    failure: Option<String>,
}

/// Reconfigures an existing build for an extracted plugin, runs package tests, and restores the
/// default plugin cache path. This does not issue a build command.
///
/// # Errors
///
/// Returns an error on admission, probe, or cleanup failure. A typed report is written before
/// returning a probe or cleanup failure.
pub fn run_package_probe(
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
    plugin_path: &Path,
    runner: &mut dyn Runner,
) -> Result<NativeTestEvidence> {
    let prepared = prepare_probe(plan, context, required_tests, options, plugin_path)?;
    let mut native_report = new_native_report(plan, context, required_tests);
    let commands = prepare_probe_commands(plan, context, options, &prepared)?;
    let outcome = execute_probe(
        &prepared,
        commands,
        context,
        required_tests,
        runner,
        &mut native_report,
    );
    let evidence = NativeTestEvidence {
        schema_version: 1,
        key: plan.key,
        source: context.source.clone(),
        status: if outcome.failure.is_some() {
            NativeStatus::Failed
        } else {
            NativeStatus::Passed
        },
        cleanup_status: outcome.cleanup_status,
        required_tests: required_tests.to_vec(),
        discovered_tests: native_report.discovered_tests,
        executed_tests: native_report.executed_tests,
        steps: native_report.steps,
        junit_path: prepared.junit_path,
        diagnostic_paths: native_report.diagnostic_paths,
        failure: outcome.failure,
    };
    write_json_report(&prepared.report_path, &evidence).with_context(|| {
        format!(
            "could not write package probe report {}",
            prepared.report_path.display()
        )
    })?;
    ensure!(
        evidence.status == NativeStatus::Passed,
        "package probe failed; see {}: {}",
        prepared.report_path.display(),
        evidence.failure.as_deref().unwrap_or("unknown failure")
    );
    Ok(evidence)
}

fn prepare_probe(
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
    plugin_path: &Path,
) -> Result<PreparedProbe> {
    validate_probe_selection(plan, context, required_tests, options)?;
    let plugin_path = require_regular_file(plugin_path, "extracted package plugin")?;
    let mut probe_options = options.clone();
    probe_options.package_plugin = Some(plugin_path);
    validate_typed_paths(plan.key.product, &probe_options)?;

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
        "package probe plan does not match fixed product/check mapping"
    );
    let build_metadata = fs::symlink_metadata(&plan.build_directory).with_context(|| {
        format!(
            "package probe requires existing build {}",
            plan.build_directory.display()
        )
    })?;
    ensure!(
        build_metadata.is_dir() && !build_metadata.file_type().is_symlink(),
        "package probe build directory must be a regular non-symlink directory"
    );
    let build_directory = dunce::canonicalize(&plan.build_directory)?;
    ensure!(
        build_directory.starts_with(&output),
        "package probe build escaped output root"
    );
    let source_directory = dunce::canonicalize(&plan.source_directory)?;
    ensure!(
        source_directory == plan.source_directory,
        "package probe source must be canonical"
    );

    let key_slug = build_key_slug(plan.key);
    let log_directory = create_child_directory(&output, PathBuf::from("logs").join(&key_slug))?;
    let report_directory = create_child_directory(&output, PathBuf::from("reports"))?;
    let junit_directory = create_child_directory(&output, PathBuf::from("junit"))?;
    Ok(PreparedProbe {
        probe_options,
        source_directory,
        build_directory,
        log_directory,
        report_path: report_directory.join(format!("{key_slug}-package-probe.json")),
        junit_path: junit_directory.join(format!("{key_slug}-package-probe.xml")),
    })
}

fn validate_probe_selection(
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
) -> Result<()> {
    validate_host()?;
    ensure!(
        context.toolset.platform() == "windows-x64",
        "native package qualification requires a Windows x64 toolset"
    );
    ensure!(context.jobs.get() <= 256, "jobs must be between 1 and 256");
    validate_required_tests(required_tests)?;
    let expected_tests = required_plugin_package_tests(plan.key.product)
        .iter()
        .map(|name| (*name).to_owned())
        .collect::<Vec<_>>();
    ensure!(
        !expected_tests.is_empty(),
        "the foundation product has no adapter plugin package probe"
    );
    ensure!(
        required_tests == expected_tests,
        "package probe tests do not match exact product package test identities"
    );
    ensure!(
        options.package_plugin.is_none(),
        "package probe options cannot override the selected plugin"
    );
    if options.release_build {
        reject_release_source_overrides()?;
    }
    Ok(())
}

fn new_native_report(
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
) -> NativeReport {
    NativeReport {
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
    }
}

fn prepare_probe_commands(
    plan: &BuildPlan,
    context: &ExecutionContext,
    options: &NativeOptions,
    prepared: &PreparedProbe,
) -> Result<ProbeCommands> {
    let cmake = require_tool(&context.toolset, "cmake")?;
    let ctest = require_sibling_tool(&cmake, if cfg!(windows) { "ctest.exe" } else { "ctest" })?;
    let environment = command_environment(&context.toolset)?;
    let configure_args = configure_arguments(
        plan,
        &prepared.build_directory,
        &prepared.probe_options,
        &context.toolset,
    )?;
    let restore_arguments =
        configure_arguments(plan, &prepared.build_directory, options, &context.toolset)?;
    Ok(ProbeCommands {
        cmake,
        ctest,
        environment,
        configure_arguments: configure_args,
        restore_arguments,
    })
}

fn execute_probe(
    prepared: &PreparedProbe,
    commands: ProbeCommands,
    context: &ExecutionContext,
    required_tests: &[String],
    runner: &mut dyn Runner,
    native_report: &mut NativeReport,
) -> ProbeOutcome {
    let mut failure = None;
    match run_step(
        runner,
        native_report,
        &prepared.log_directory,
        "package-probe-configure",
        commands.cmake.clone(),
        commands.configure_arguments,
        &prepared.source_directory,
        commands.environment.clone(),
        true,
    ) {
        Ok(step) if step.status == NativeStatus::Passed => {
            if let Err(error) = run_test_suite(
                &prepared.source_directory,
                &prepared.build_directory,
                &prepared.log_directory,
                &prepared.junit_path,
                context,
                required_tests,
                TestScope::Required,
                "package-probe-",
                commands.ctest,
                &commands.environment,
                runner,
                native_report,
            ) {
                failure = Some(format!("{error:#}"));
            }
        }
        Ok(step) => {
            failure = Some(format!(
                "package probe configure exited {:?}",
                step.exit_code
            ));
        }
        Err(error) => failure = Some(format!("{error:#}")),
    }

    let cleanup = run_step(
        runner,
        native_report,
        &prepared.log_directory,
        "package-probe-restore",
        commands.cmake,
        commands.restore_arguments,
        &prepared.source_directory,
        commands.environment,
        true,
    );
    let (cleanup_status, cleanup_failure) = match cleanup {
        Ok(step) => (
            step.status,
            (step.status != NativeStatus::Passed)
                .then(|| format!("package probe cache restore exited {:?}", step.exit_code)),
        ),
        Err(error) => (NativeStatus::Failed, Some(format!("{error:#}"))),
    };
    if let Some(cleanup_failure) = cleanup_failure {
        failure = Some(match failure {
            Some(primary) => format!("{primary}; cleanup also failed: {cleanup_failure}"),
            None => cleanup_failure,
        });
    }
    ProbeOutcome {
        cleanup_status,
        failure,
    }
}
