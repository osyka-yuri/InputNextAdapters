//! Validate embedded source dependency identities and complete inventories.

use std::collections::BTreeSet;
use std::fs;
use std::path::Path;

use anyhow::{Context, Result, ensure};

use crate::model::Product;
use crate::tooling::hash_file;

use super::catalog::NativeDependency;
use super::filesystem;
use super::types::{SourceBundleManifest, SourceDependency, SourceSubmodule};

pub(in crate::package) fn validate_source_bundle_manifest(
    root: &Path,
    product: Product,
) -> Result<()> {
    let Some(mut expected) = super::catalog::dependencies_for(product)? else {
        ensure!(
            !root.join("inputnext-adapters-source.json").exists()
                && !root.join("dependencies").exists(),
            "product source package has undeclared external dependencies"
        );
        return Ok(());
    };
    let manifest_path = root.join("inputnext-adapters-source.json");
    let metadata = fs::symlink_metadata(&manifest_path)
        .context("source package is missing inputnext-adapters-source.json")?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= super::MAX_RECEIPT_BYTES,
        "source dependency manifest must be a bounded regular file"
    );
    let manifest: SourceBundleManifest = crate::json::parse(&fs::read(&manifest_path)?)
        .context("source dependency manifest is malformed")?;
    ensure!(
        manifest.schema_version == 1
            && manifest.kind == "inputnext-adapters-source-bundle"
            && manifest.product == product.as_str(),
        "source dependency manifest has another schema or product"
    );
    expected.sort_by(|left, right| left.name.cmp(&right.name));
    validate_source_dependencies(root, &manifest.dependencies, &expected)
}

fn validate_source_dependencies(
    root: &Path,
    dependencies: &[SourceDependency],
    expected: &[NativeDependency],
) -> Result<()> {
    ensure!(
        dependencies.len() == expected.len(),
        "source dependency manifest is incomplete or contains extra dependencies"
    );
    let mut dependency_files = BTreeSet::new();
    for (entry, expected) in dependencies.iter().zip(expected) {
        validate_source_dependency(root, entry, expected)?;
        for file in &entry.files {
            dependency_files.insert(format!("{}/{}", entry.path, file.path));
        }
    }
    validate_dependency_inventory(root, &dependency_files)
}

fn validate_source_dependency(
    root: &Path,
    entry: &SourceDependency,
    expected: &NativeDependency,
) -> Result<()> {
    ensure!(
        entry.name == expected.name
            && entry.repository == expected.repository
            && entry.revision == expected.revision
            && entry.path == expected.relative_path
            && entry.fetch_content == expected.fetch_content,
        "source dependency identity differs from the frozen {} pin",
        expected.name
    );
    let dependency_root = filesystem::safe_bundle_path(root, &entry.path)?;
    let actual_files = filesystem::inventory_dependency_files(&dependency_root)?;
    ensure!(
        entry.files.iter().map(|file| &file.path).is_sorted(),
        "source dependency file inventory must be sorted"
    );
    ensure!(
        actual_files == entry.files,
        "source dependency file inventory or SHA-256 differs for {}",
        expected.name
    );
    validate_source_submodules(&dependency_root, entry, expected)?;
    validate_source_patches(&dependency_root, entry, expected)
}

fn validate_source_submodules(
    dependency_root: &Path,
    entry: &SourceDependency,
    expected: &NativeDependency,
) -> Result<()> {
    let expected_submodules = expected
        .submodules
        .iter()
        .map(|submodule| SourceSubmodule {
            name: submodule.name.clone(),
            path: submodule.path.clone(),
            repository: submodule.repository.clone(),
            revision: submodule.revision.clone(),
        })
        .collect::<Vec<_>>();
    ensure!(
        entry.submodules == expected_submodules,
        "source dependency submodule identity differs for {}",
        expected.name
    );
    for submodule in &expected.submodules {
        let _ = filesystem::safe_bundle_path(dependency_root, &submodule.path)?;
    }
    Ok(())
}

fn validate_source_patches(
    dependency_root: &Path,
    entry: &SourceDependency,
    expected: &NativeDependency,
) -> Result<()> {
    ensure!(
        entry.patches.len() == expected.patches.len(),
        "source dependency amendment set differs for {}",
        expected.name
    );
    for (actual, expected_patch) in entry.patches.iter().zip(&expected.patches) {
        ensure!(
            actual.id == expected_patch.id
                && actual.path == expected_patch.path
                && (actual.sha256 == expected_patch.output_sha256.lf
                    || actual.sha256 == expected_patch.output_sha256.crlf),
            "source dependency amendment differs from the approved {} patch",
            expected.name
        );
        let patch_path = filesystem::safe_bundle_path(dependency_root, &actual.path)?;
        ensure!(
            hash_file(&patch_path)? == actual.sha256,
            "source dependency amendment file hash mismatch"
        );
    }
    Ok(())
}

fn validate_dependency_inventory(root: &Path, expected_files: &BTreeSet<String>) -> Result<()> {
    let dependency_root = root.join("dependencies");
    let actual_dependency_files = if dependency_root.exists() {
        filesystem::inventory_dependency_paths(&dependency_root)?
            .into_iter()
            .map(|path| format!("dependencies/{path}"))
            .collect::<BTreeSet<_>>()
    } else {
        BTreeSet::new()
    };
    ensure!(
        expected_files == &actual_dependency_files,
        "source package dependency tree has missing or undeclared files"
    );
    Ok(())
}
