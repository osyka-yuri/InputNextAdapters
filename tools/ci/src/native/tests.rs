use super::execution::{TestScope, test_scope};
use super::{
    configure_arguments, ctest_filter_arguments, needs_repository_quality_check, run_step,
    validate_required_tests,
};
use crate::model::{
    Architecture, BuildKey, Check, NativeOptions, NativeReport, NativeStatus, NativeStep, Product,
    SourceIdentity,
};
use crate::plan::create_plan;
use crate::process::{CommandOutput, CommandSpec, Runner};
use anyhow::Result;
use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

struct FailedStepRunner;

impl Runner for FailedStepRunner {
    fn run(&mut self, _spec: &CommandSpec) -> Result<CommandOutput> {
        Ok(CommandOutput {
            code: Some(1),
            stdout: b"configure stdout\n".to_vec(),
            stderr: b"compiler failure\n".to_vec(),
        })
    }
}

#[test]
fn required_test_names_are_exact_and_bounded() -> Result<()> {
    validate_required_tests(&["runtime_client.production_package".to_owned()])?;
    assert!(validate_required_tests(&["output_session.package.*".to_owned()]).is_err());
    assert!(validate_required_tests(&[String::new()]).is_err());
    assert!(
        validate_required_tests(&[
            "skyrim.package.one".to_owned(),
            "skyrim.package.one".to_owned(),
        ])
        .is_err()
    );
    Ok(())
}

#[test]
fn ctest_filter_selects_only_required_exact_names() -> Result<()> {
    let required = vec![
        "runtime_client.production_package".to_owned(),
        "output_session.package".to_owned(),
    ];
    assert_eq!(
        ctest_filter_arguments(&required)?,
        vec![
            OsString::from("-R"),
            OsString::from(r"^(runtime_client\.production_package|output_session\.package)$"),
        ]
    );
    assert!(ctest_filter_arguments(&[]).is_ok_and(|arguments| arguments.is_empty()));
    Ok(())
}

#[test]
fn required_package_cases_do_not_narrow_regular_native_suites() {
    let options = NativeOptions::default();
    for check in [
        Check::Tests,
        Check::NativeTests,
        Check::NativeQuality,
        Check::NativeAnalyze,
    ] {
        assert_eq!(test_scope(check, &options), TestScope::All);
    }
    assert_eq!(test_scope(Check::Package, &options), TestScope::Required);
    let qualification = NativeOptions {
        runtime_directory: Some(PathBuf::from("runtime/bin")),
        ..options
    };
    assert_eq!(
        test_scope(Check::Tests, &qualification),
        TestScope::Required
    );
    assert_eq!(
        test_scope(Check::NativeQuality, &qualification),
        TestScope::All
    );
}

#[test]
fn foundation_and_native_quality_plans_run_repository_quality_checks() {
    assert!(needs_repository_quality_check(Check::Quality));
    assert!(needs_repository_quality_check(Check::NativeQuality));
    assert!(!needs_repository_quality_check(Check::Tests));
}

#[test]
fn configure_arguments_clear_absent_paths_and_keep_typed_paths_as_single_values() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let repository = temporary.path();
    let options = NativeOptions::default();
    let plan = create_plan(
        repository,
        Path::new("build/ci"),
        Product::SkyrimSe,
        Architecture::X64,
        Check::Package,
        &options,
    )?;
    let toolset = llvm_toolset(repository)?;
    let args = configure_arguments(&plan, &plan.build_directory, &options, &toolset)?;
    let strings = args
        .iter()
        .map(|argument| argument.to_string_lossy().into_owned())
        .collect::<Vec<_>>();
    assert!(strings.contains(&"-DINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=OFF".to_owned()));
    assert!(!strings.contains(&"-DINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON".to_owned()));
    assert!(strings.contains(&"-DINPUTNEXTADAPTERS_RUNTIME_DIRECTORY:PATH=".to_owned()));
    assert!(strings.contains(&"-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:FILEPATH=".to_owned()));
    assert!(strings.contains(&"-DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=".to_owned()));

    let release_options = NativeOptions {
        release_build: true,
        ..options
    };
    let release_args =
        configure_arguments(&plan, &plan.build_directory, &release_options, &toolset)?;
    let release_strings = release_args
        .iter()
        .map(|argument| argument.to_string_lossy().into_owned())
        .collect::<Vec<_>>();
    assert!(release_strings.contains(&"-DINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON".to_owned()));
    assert!(release_strings.contains(&"-DINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON".to_owned()));

    let runtime = repository.join("runtime bin");
    fs::create_dir_all(&runtime)?;
    fs::write(runtime.join("inputnext_runtime.dll"), b"runtime")?;
    let plugin = repository.join("plugin file.dll");
    fs::write(&plugin, b"plugin")?;
    let bundle = repository.join("inputnext-adapters-source.json");
    fs::write(&bundle, b"{}")?;
    let configured = NativeOptions {
        runtime_directory: Some(runtime),
        package_plugin: Some(plugin),
        source_bundle: Some(bundle),
        release_build: false,
    };
    let args = configure_arguments(&plan, &plan.build_directory, &configured, &toolset)?;
    let plugin_arg = args
        .iter()
        .find(|argument| {
            argument
                .to_string_lossy()
                .starts_with("-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:")
        })
        .ok_or_else(|| anyhow::anyhow!("package plugin CMake argument is missing"))?;
    assert!(plugin_arg.to_string_lossy().contains("plugin file.dll"));
    assert!(
        plugin_arg
            .to_string_lossy()
            .starts_with("-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:FILEPATH=")
    );
    Ok(())
}

#[test]
fn clang_profiles_use_the_verified_llvm_compiler_for_c_and_cxx() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let repository = temporary.path();
    let toolset = llvm_toolset(repository)?;
    let compiler = dunce::canonicalize(repository.join(format!(
        "pinned llvm/bin/clang-cl{}",
        if cfg!(windows) { ".exe" } else { "" }
    )))?;

    for (product, architecture, check) in [
        (Product::Foundation, Architecture::X64, Check::Ubsan),
        (Product::SkyrimLe, Architecture::X86, Check::ClangTests),
    ] {
        let plan = create_plan(
            repository,
            Path::new("build/ci"),
            product,
            architecture,
            check,
            &NativeOptions::default(),
        )?;
        let args = configure_arguments(
            &plan,
            &plan.build_directory,
            &NativeOptions::default(),
            &toolset,
        )?;
        let strings = args
            .iter()
            .map(|argument| argument.to_string_lossy().into_owned())
            .collect::<Vec<_>>();
        for variable in ["CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"] {
            let expected = format!("-D{variable}:FILEPATH={}", compiler.display());
            assert!(
                strings.contains(&expected),
                "missing {expected} in {strings:?}"
            );
        }
    }
    Ok(())
}

#[test]
fn skyrim_le_clang_plan_targets_i686_for_c_and_cxx() -> Result<()> {
    let repository = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let plan = create_plan(
        &repository,
        Path::new("build/ci"),
        Product::SkyrimLe,
        Architecture::X86,
        Check::ClangTests,
        &NativeOptions::default(),
    )?;
    let presets: serde_json::Value = serde_json::from_str(&fs::read_to_string(
        plan.source_directory.join("CMakePresets.json"),
    )?)?;
    let configure_presets = presets["configurePresets"]
        .as_array()
        .ok_or_else(|| anyhow::anyhow!("missing configure presets"))?;
    let preset = configure_presets
        .iter()
        .find(|preset| preset["name"] == plan.preset.as_str())
        .ok_or_else(|| anyhow::anyhow!("missing configure preset '{}'", plan.preset))?;
    for variable in ["CMAKE_C_COMPILER_TARGET", "CMAKE_CXX_COMPILER_TARGET"] {
        assert_eq!(
            preset["cacheVariables"][variable].as_str(),
            Some("i686-pc-windows-msvc"),
            "{} must set {variable} before compiler detection",
            plan.preset
        );
    }
    Ok(())
}

fn llvm_toolset(root: &Path) -> Result<crate::tooling::Toolset> {
    let bin = root.join("pinned llvm/bin");
    fs::create_dir_all(&bin)?;
    let extension = if cfg!(windows) { ".exe" } else { "" };
    let llvm_clang_tidy = bin.join(format!("clang-tidy{extension}"));
    let clang_cl = bin.join(format!("clang-cl{extension}"));
    fs::write(&llvm_clang_tidy, b"pinned clang-tidy")?;
    fs::write(&clang_cl, b"pinned clang-cl")?;

    let report = root.join("toolset.json");
    let hash = "0".repeat(64);
    fs::write(
        &report,
        serde_json::to_vec(&serde_json::json!({
            "schema_version": 1,
            "profile": "quality",
            "platform": "windows-x64",
            "lock_sha256": hash,
            "tools": {
                "llvm": {
                    "version": "23.1.5",
                    "path": llvm_clang_tidy,
                    "kind": "executable",
                    "archive": null,
                    "sha256": hash
                }
            },
            "environment": {}
        }))?,
    )?;
    crate::tooling::load_toolset(&report)
}

#[test]
fn failed_native_step_persists_both_bounded_diagnostic_streams() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let mut report = NativeReport {
        schema_version: 1,
        key: BuildKey {
            product: Product::Foundation,
            architecture: Architecture::X64,
            check: Check::Tests,
        },
        source: SourceIdentity {
            commit: None,
            modified: true,
            snapshot_sha256: Some("0".repeat(64)),
        },
        status: NativeStatus::Failed,
        required_tests: Vec::new(),
        discovered_tests: Vec::new(),
        executed_tests: Vec::new(),
        steps: Vec::<NativeStep>::new(),
        junit_path: None,
        sarif_paths: Vec::new(),
        diagnostic_paths: Vec::new(),
        failure: None,
    };
    let mut runner = FailedStepRunner;
    let step = run_step(
        &mut runner,
        &mut report,
        temporary.path(),
        "configure",
        PathBuf::from("cmake.exe"),
        Vec::new(),
        temporary.path(),
        BTreeMap::new(),
        true,
    )?;
    assert_eq!(step.status, NativeStatus::Failed);
    assert_eq!(fs::read(&step.stdout_log)?, b"configure stdout\n");
    assert_eq!(fs::read(&step.stderr_log)?, b"compiler failure\n");
    assert_eq!(step.exit_code, Some(1));
    Ok(())
}

#[test]
fn status_values_are_stable_report_types() {
    assert_eq!(
        serde_json::to_string(&NativeStatus::Passed).unwrap_or_default(),
        "\"passed\""
    );
}
