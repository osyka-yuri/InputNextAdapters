//! Assemble the product-owned source tree and pinned native dependency sources.

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};

use crate::model::Product;
use crate::process::Runner;
use crate::tooling::write_json_report;

use super::catalog::NativeDependency;
use super::filesystem;
use super::types::{
    DependencyReceipt, ReceiptDependency, SourceBundleManifest, SourceDependency, SourceSubmodule,
};

pub(in crate::package) fn copy_source_bundle(
    product: Product,
    repository: &Path,
    build_directory: &Path,
    destination: &Path,
    runner: &mut dyn Runner,
) -> Result<()> {
    let git = filesystem::find_git()?;
    copy_authored_source(repository, destination, runner, &git)?;

    if let Some(expected) = super::catalog::dependencies_for(product)? {
        copy_dependency_sources(
            product,
            &expected,
            build_directory,
            destination,
            &git,
            runner,
        )?;
    }
    if product == Product::SkyrimLe {
        ensure!(
            destination
                .join("adapters/skyrim_le/docs/validation.md")
                .is_file(),
            "Skyrim LE source package must include the official SKSE acquisition instructions"
        );
    }
    Ok(())
}

fn copy_authored_source(
    repository: &Path,
    destination: &Path,
    runner: &mut dyn Runner,
    git: &Path,
) -> Result<()> {
    for relative in filesystem::list_authored_paths(runner, git, repository)? {
        let source = repository.join(&relative);
        let metadata = fs::symlink_metadata(&source)
            .with_context(|| format!("could not inspect source file {}", source.display()))?;
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "source package contains a non-regular authored path: {relative}"
        );
        let output = destination.join(&relative);
        if let Some(parent) = output.parent() {
            fs::create_dir_all(parent).with_context(|| {
                format!(
                    "could not create source package directory {}",
                    parent.display()
                )
            })?;
        }
        fs::copy(&source, &output)
            .with_context(|| format!("could not copy source file {relative}"))?;
    }
    Ok(())
}

fn copy_dependency_sources(
    product: Product,
    expected: &[NativeDependency],
    build_directory: &Path,
    destination: &Path,
    git: &Path,
    runner: &mut dyn Runner,
) -> Result<()> {
    let receipt = read_receipt(build_directory, product)?;
    let dependencies_root = dunce::canonicalize(build_directory.join("_deps"))
        .context("CMake dependency sources must live under the build tree _deps directory")?;
    let by_name = receipt
        .dependencies
        .iter()
        .map(|dependency| (dependency.name.as_str(), dependency))
        .collect::<BTreeMap<_, _>>();
    ensure!(
        by_name.len() == receipt.dependencies.len(),
        "CMake dependency receipt contains duplicate names"
    );
    let expected_receipt_count = expected.len()
        + expected
            .iter()
            .map(|dependency| dependency.submodules.len())
            .sum::<usize>();
    ensure!(
        receipt.dependencies.len() == expected_receipt_count,
        "CMake dependency receipt is incomplete or contains unexpected sources"
    );

    let mut manifest_dependencies = expected
        .iter()
        .map(|dependency| {
            copy_one_dependency(
                dependency,
                &dependencies_root,
                destination,
                &by_name,
                git,
                runner,
            )
        })
        .collect::<Result<Vec<_>>>()?;
    manifest_dependencies.sort_by(|left, right| left.name.cmp(&right.name));
    let manifest = SourceBundleManifest {
        schema_version: 1,
        kind: "inputnext-adapters-source-bundle".to_owned(),
        product: product.as_str().to_owned(),
        dependencies: manifest_dependencies,
    };
    write_json_report(
        &destination.join("inputnext-adapters-source.json"),
        &manifest,
    )?;
    Ok(())
}

fn copy_one_dependency(
    expected: &NativeDependency,
    dependencies_root: &Path,
    destination: &Path,
    receipt_by_name: &BTreeMap<&str, &ReceiptDependency>,
    git: &Path,
    runner: &mut dyn Runner,
) -> Result<SourceDependency> {
    let receipt = receipt_by_name
        .get(expected.name.as_str())
        .with_context(|| format!("CMake receipt is missing {}", expected.name))?;
    ensure!(
        receipt.repository == expected.repository && receipt.revision == expected.revision,
        "CMake dependency receipt identity differs from the pinned {} source",
        expected.name
    );
    let source_root = dunce::canonicalize(&receipt.source_directory)
        .with_context(|| format!("could not resolve {} source directory", expected.name))?;
    ensure!(
        source_root.starts_with(dependencies_root) && source_root.is_dir(),
        "CMake dependency source escaped the build tree _deps directory"
    );
    let relative_source = source_root.strip_prefix(dependencies_root)?;
    let expected_source_relative = PathBuf::from(format!(
        "{}-src",
        expected.fetch_content.to_ascii_lowercase()
    ));
    ensure!(
        relative_source == expected_source_relative,
        "CMake dependency source path is not its fixed _deps location"
    );
    ensure!(
        filesystem::git_revision_matches(runner, git, &source_root, &expected.revision)?,
        "source packaging requires the pinned Git revision for {}",
        expected.name
    );
    validate_receipt_patches(expected, receipt)?;

    let output_root = destination.join(&expected.relative_path);
    let files = filesystem::copy_dependency_tree(&expected.name, &source_root, &output_root)?;
    let submodules =
        copy_dependency_submodules(expected, &source_root, receipt_by_name, git, runner)?;
    Ok(SourceDependency {
        name: expected.name.clone(),
        repository: expected.repository.clone(),
        revision: expected.revision.clone(),
        path: expected.relative_path.clone(),
        fetch_content: expected.fetch_content.clone(),
        patches: receipt.patches.clone(),
        submodules,
        files,
    })
}

fn validate_receipt_patches(
    expected: &NativeDependency,
    receipt: &ReceiptDependency,
) -> Result<()> {
    ensure!(
        receipt.patches.len() == expected.patches.len(),
        "CMake receipt has an unexpected amendment set for {}",
        expected.name
    );
    for (actual, expected_patch) in receipt.patches.iter().zip(&expected.patches) {
        ensure!(
            actual.id == expected_patch.id
                && actual.path == expected_patch.path
                && (actual.sha256 == expected_patch.output_sha256.lf
                    || actual.sha256 == expected_patch.output_sha256.crlf),
            "CMake receipt has an unexpected amendment for {}",
            expected.name
        );
    }
    Ok(())
}

fn copy_dependency_submodules(
    expected: &NativeDependency,
    source_root: &Path,
    receipt_by_name: &BTreeMap<&str, &ReceiptDependency>,
    git: &Path,
    runner: &mut dyn Runner,
) -> Result<Vec<SourceSubmodule>> {
    let mut submodules = Vec::with_capacity(expected.submodules.len());
    for expected_submodule in &expected.submodules {
        let receipt = receipt_by_name
            .get(expected_submodule.name.as_str())
            .with_context(|| {
                format!(
                    "CMake receipt is missing submodule {}",
                    expected_submodule.name
                )
            })?;
        let expected_root = dunce::canonicalize(source_root.join(&expected_submodule.path))
            .context("could not resolve dependency submodule directory")?;
        ensure!(
            dunce::canonicalize(&receipt.source_directory)? == expected_root
                && receipt.repository == expected_submodule.repository
                && receipt.revision == expected_submodule.revision
                && receipt.patches.is_empty(),
            "CMake dependency submodule identity differs from the frozen pin"
        );
        ensure!(
            filesystem::git_revision_matches(
                runner,
                git,
                &expected_root,
                &expected_submodule.revision
            )?,
            "source packaging requires the pinned {} submodule Git revision",
            expected_submodule.name
        );
        submodules.push(SourceSubmodule {
            name: expected_submodule.name.clone(),
            path: expected_submodule.path.clone(),
            repository: expected_submodule.repository.clone(),
            revision: expected_submodule.revision.clone(),
        });
    }
    Ok(submodules)
}

pub(in crate::package) fn authored_source_paths(
    root: &Path,
    runner: &mut dyn Runner,
) -> Result<Vec<String>> {
    let git = filesystem::find_git()?;
    filesystem::list_authored_paths(runner, &git, root)
}

fn read_receipt(build_directory: &Path, product: Product) -> Result<DependencyReceipt> {
    let path = build_directory.join("inputnext-adapters-dependency-sources.json");
    let metadata = fs::symlink_metadata(&path).with_context(|| {
        format!(
            "native build is missing dependency source receipt {}",
            path.display()
        )
    })?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= super::MAX_RECEIPT_BYTES,
        "CMake dependency source receipt must be a bounded regular file"
    );
    let bytes = fs::read(&path).context("could not read CMake dependency source receipt")?;
    let receipt: DependencyReceipt = crate::json::parse(&bytes)
        .context("CMake dependency source receipt is not valid strict JSON")?;
    ensure!(
        receipt.schema_version == 1 && receipt.product == product.as_str(),
        "CMake dependency source receipt has another schema or product"
    );
    Ok(receipt)
}
