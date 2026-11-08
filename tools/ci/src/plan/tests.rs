use super::{
    MatrixArgs, MatrixRow, create_plan, matrix_for_changes, matrix_for_product, supported_keys,
};
use crate::model::{Architecture, Check, NativeOptions, Product};
use anyhow::Result;
use clap::Parser;
use std::path::Path;

#[derive(Debug, Parser)]
struct TestMatrixCli {
    #[command(flatten)]
    args: MatrixArgs,
}

#[test]
fn planner_maps_each_supported_product_and_rejects_unsupported_abi() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let root = temporary.path();
    let options = NativeOptions::default();
    for key in supported_keys() {
        let plan = create_plan(
            root,
            Path::new("build/ci"),
            key.product,
            key.architecture,
            key.check,
            &options,
        )?;
        assert_eq!(plan.key, key);
        assert!(plan.build_directory.to_string_lossy().contains("builds"));
    }
    assert!(
        create_plan(
            root,
            Path::new("build/ci"),
            Product::SkyrimSe,
            Architecture::X86,
            Check::Tests,
            &options
        )
        .is_err()
    );
    assert!(
        create_plan(
            root,
            Path::new("build/ci"),
            Product::Foundation,
            Architecture::X86,
            Check::Asan,
            &options
        )
        .is_err()
    );
    Ok(())
}

#[test]
fn runtime_and_plugin_test_names_are_exact_and_full() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let root = temporary.path();
    let options = NativeOptions {
        runtime_directory: Some(root.join("runtime/bin")),
        package_plugin: Some(root.join("package/plugin.dll")),
        ..NativeOptions::default()
    };
    let plan = create_plan(
        root,
        Path::new("build/ci"),
        Product::SkyrimSe,
        Architecture::X64,
        Check::Package,
        &options,
    )?;
    assert!(
        plan.required_tests
            .contains(&"skyrim.package.classic_casting_requests_validate_cold".to_owned())
    );
    assert!(
        plan.required_tests
            .contains(&"skyrim_plugin_package".to_owned())
    );
    assert!(!plan.required_tests.iter().any(|name| name.ends_with(".*")));
    Ok(())
}

#[test]
fn source_bundle_and_release_are_mutually_exclusive() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let options = NativeOptions {
        source_bundle: Some(temporary.path().join("bundle/source.json")),
        release_build: true,
        ..NativeOptions::default()
    };
    assert!(
        create_plan(
            temporary.path(),
            Path::new("build/ci"),
            Product::Foundation,
            Architecture::X64,
            Check::Package,
            &options
        )
        .is_err()
    );
    Ok(())
}

#[test]
fn change_closure_selects_only_dependent_products_and_unknown_is_conservative() {
    let shared_skyrim = vec!["adapters/skyrim/src/native/events.cpp".to_owned()];
    let matrix = matrix_for_changes(Some(&shared_skyrim), false);
    assert!(
        matrix
            .include
            .iter()
            .all(|row| matches!(row.product, Product::SkyrimSe | Product::SkyrimLe))
    );
    assert!(
        matrix
            .include
            .iter()
            .any(|row| row.product == Product::SkyrimSe)
    );
    assert!(
        matrix
            .include
            .iter()
            .any(|row| row.product == Product::SkyrimLe)
    );

    let foundation_common = vec!["include/inputnext/runtime.hpp".to_owned()];
    let matrix = matrix_for_changes(Some(&foundation_common), false);
    for product in [
        Product::Foundation,
        Product::SkyrimSe,
        Product::SkyrimLe,
        Product::Fallout4,
    ] {
        assert!(matrix.include.iter().any(|row| row.product == product));
    }

    let game_specific = vec!["adapters/fallout4/src/plugin.cpp".to_owned()];
    let matrix = matrix_for_changes(Some(&game_specific), false);
    assert!(
        matrix
            .include
            .iter()
            .all(|row| row.product == Product::Fallout4)
    );

    let unknown = vec!["new-root-build-mechanism.txt".to_owned()];
    let matrix = matrix_for_changes(Some(&unknown), false);
    assert_eq!(
        matrix.include.len(),
        supported_keys()
            .into_iter()
            .filter(|key| key.check != Check::NativeTests)
            .count()
    );
    let malformed = vec!["adapters/fallout4//src/plugin.cpp".to_owned()];
    assert_eq!(
        matrix_for_changes(Some(&malformed), false).include.len(),
        supported_keys()
            .into_iter()
            .filter(|key| key.check != Check::NativeTests)
            .count()
    );
}

#[test]
fn empty_changes_are_an_explicit_skip_and_deep_stays_within_the_closure() {
    let empty: Vec<String> = Vec::new();
    assert!(matrix_for_changes(Some(&empty), false).include.is_empty());
    assert!(matrix_for_changes(Some(&empty), true).include.is_empty());
    let paths = vec!["adapters/fallout4/src/plugin.cpp".to_owned()];
    let deep = matrix_for_changes(Some(&paths), true);
    assert!(!deep.include.is_empty());
    assert!(
        deep.include
            .iter()
            .all(|row| row.product == Product::Fallout4)
    );
    assert_eq!(deep.include.len(), 7);
    assert!(
        deep.include
            .iter()
            .all(|row: &MatrixRow| row.check != Check::NativeTests)
    );
}

#[test]
fn clang_profiles_select_the_pinned_llvm_toolchain() {
    let matrix = matrix_for_changes(None, true);
    let ubsan_rows = matrix
        .include
        .iter()
        .filter(|row| row.check == Check::Ubsan)
        .collect::<Vec<_>>();
    assert!(!ubsan_rows.is_empty());
    assert!(ubsan_rows.iter().all(|row| row.tool_profile == "quality"));

    let clang_test_rows = matrix
        .include
        .iter()
        .filter(|row| row.check == Check::ClangTests)
        .collect::<Vec<_>>();
    assert!(!clang_test_rows.is_empty());
    assert!(
        clang_test_rows
            .iter()
            .all(|row| row.tool_profile == "quality")
    );
}

fn row_key(row: &MatrixRow) -> (Product, Architecture, Check, &'static str) {
    (row.product, row.architecture, row.check, row.tool_profile)
}

#[test]
fn matrix_for_product_fallout4_selects_exact_closure() {
    let fallout4_deep = matrix_for_product(Product::Fallout4, true);
    let expected_f4_deep = vec![
        (Product::Fallout4, Architecture::X64, Check::Tests, "build"),
        (
            Product::Fallout4,
            Architecture::X64,
            Check::Analyze,
            "build",
        ),
        (Product::Fallout4, Architecture::X64, Check::Asan, "build"),
        (
            Product::Fallout4,
            Architecture::X64,
            Check::Ubsan,
            "quality",
        ),
        (
            Product::Fallout4,
            Architecture::X64,
            Check::NativeQuality,
            "quality",
        ),
        (
            Product::Fallout4,
            Architecture::X64,
            Check::NativeAnalyze,
            "build",
        ),
        (
            Product::Fallout4,
            Architecture::X64,
            Check::Package,
            "release",
        ),
    ];
    assert_eq!(
        fallout4_deep
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_f4_deep
    );

    let fallout4_shallow = matrix_for_product(Product::Fallout4, false);
    let expected_f4_shallow = vec![
        (
            Product::Fallout4,
            Architecture::X64,
            Check::NativeQuality,
            "quality",
        ),
        (Product::Fallout4, Architecture::X64, Check::Tests, "build"),
    ];
    assert_eq!(
        fallout4_shallow
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_f4_shallow
    );
}

#[test]
fn matrix_for_product_skyrim_se_selects_exact_closure() {
    let skyrim_se_deep = matrix_for_product(Product::SkyrimSe, true);
    let expected_se_deep = vec![
        (Product::SkyrimSe, Architecture::X64, Check::Tests, "build"),
        (
            Product::SkyrimSe,
            Architecture::X64,
            Check::Analyze,
            "build",
        ),
        (Product::SkyrimSe, Architecture::X64, Check::Asan, "build"),
        (
            Product::SkyrimSe,
            Architecture::X64,
            Check::Ubsan,
            "quality",
        ),
        (
            Product::SkyrimSe,
            Architecture::X64,
            Check::NativeQuality,
            "quality",
        ),
        (
            Product::SkyrimSe,
            Architecture::X64,
            Check::Package,
            "release",
        ),
    ];
    assert_eq!(
        skyrim_se_deep
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_se_deep
    );

    let skyrim_se_shallow = matrix_for_product(Product::SkyrimSe, false);
    let expected_se_shallow = vec![
        (
            Product::SkyrimSe,
            Architecture::X64,
            Check::NativeQuality,
            "quality",
        ),
        (Product::SkyrimSe, Architecture::X64, Check::Tests, "build"),
    ];
    assert_eq!(
        skyrim_se_shallow
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_se_shallow
    );
}

#[test]
fn matrix_for_product_skyrim_le_selects_exact_closure() {
    let skyrim_le_deep = matrix_for_product(Product::SkyrimLe, true);
    let expected_le_deep = vec![
        (Product::SkyrimLe, Architecture::X86, Check::Tests, "build"),
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::Analyze,
            "build",
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::ClangTests,
            "quality",
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::NativeQuality,
            "quality",
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::NativeAnalyze,
            "build",
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::Package,
            "release",
        ),
    ];
    assert_eq!(
        skyrim_le_deep
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_le_deep
    );

    let skyrim_le_shallow = matrix_for_product(Product::SkyrimLe, false);
    let expected_le_shallow = vec![
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::NativeQuality,
            "quality",
        ),
        (Product::SkyrimLe, Architecture::X86, Check::Tests, "build"),
    ];
    assert_eq!(
        skyrim_le_shallow
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_le_shallow
    );
}

#[test]
fn matrix_for_product_foundation_selects_exact_closure() {
    // Foundation deep matrix must cover the full affected closure: Foundation + Skyrim SE + Skyrim LE + Fallout 4 (27 checks)
    let foundation_deep = matrix_for_product(Product::Foundation, true);
    assert_eq!(foundation_deep.include.len(), 27);
    let full = matrix_for_changes(None, true);
    assert_eq!(
        foundation_deep
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        full.include.iter().map(row_key).collect::<Vec<_>>()
    );

    let foundation_shallow = matrix_for_product(Product::Foundation, false);
    let expected_foundation_shallow = vec![
        (
            Product::Fallout4,
            Architecture::X64,
            Check::NativeQuality,
            "quality",
        ),
        (Product::Fallout4, Architecture::X64, Check::Tests, "build"),
        (
            Product::Foundation,
            Architecture::X64,
            Check::Quality,
            "quality",
        ),
        (
            Product::Foundation,
            Architecture::X64,
            Check::Tests,
            "build",
        ),
        (
            Product::Foundation,
            Architecture::X86,
            Check::Tests,
            "build",
        ),
        (
            Product::SkyrimLe,
            Architecture::X86,
            Check::NativeQuality,
            "quality",
        ),
        (Product::SkyrimLe, Architecture::X86, Check::Tests, "build"),
        (
            Product::SkyrimSe,
            Architecture::X64,
            Check::NativeQuality,
            "quality",
        ),
        (Product::SkyrimSe, Architecture::X64, Check::Tests, "build"),
    ];
    assert_eq!(
        foundation_shallow
            .include
            .iter()
            .map(row_key)
            .collect::<Vec<_>>(),
        expected_foundation_shallow
    );
}

#[test]
fn matrix_args_cli_parsing_supports_product_and_preserves_backward_compatibility() -> Result<()> {
    let parsed = TestMatrixCli::try_parse_from(["test", "--product", "fallout4", "--deep"])?;
    assert_eq!(parsed.args.product, Some(Product::Fallout4));
    assert!(parsed.args.deep);
    assert!(parsed.args.changed_paths.is_none());

    let bare = TestMatrixCli::try_parse_from(["test"])?;
    assert_eq!(bare.args.product, None);
    assert_eq!(bare.args.changed_paths, None);
    assert!(!bare.args.deep);

    assert!(
        TestMatrixCli::try_parse_from([
            "test",
            "--product",
            "fallout4",
            "--changed-paths",
            "paths.nul",
        ])
        .is_err()
    );

    assert!(TestMatrixCli::try_parse_from(["test", "--product", "invalid_game"]).is_err());
    Ok(())
}
