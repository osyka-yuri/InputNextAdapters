//! Package build, output, and report path admission.

use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};

use crate::model::{Architecture, Check, NativeOptions, Product};
use crate::plan::create_plan;

use super::MAX_REPORT_BYTES;
pub(super) fn validate_build_directory(
    repository: &Path,
    requested: &Path,
    product: Product,
    architecture: Architecture,
) -> Result<PathBuf> {
    let build_directory = dunce::canonicalize(requested).with_context(|| {
        format!(
            "could not resolve package build directory {}",
            requested.display()
        )
    })?;
    ensure!(
        build_directory.is_dir(),
        "package build directory is not a directory"
    );
    let native_output = build_directory
        .parent()
        .and_then(Path::parent)
        .context("package build directory must be output/builds/<product>-<arch>-package")?;
    let plan = create_plan(
        repository,
        native_output,
        product,
        architecture,
        Check::Package,
        &NativeOptions::default(),
    )?;
    ensure!(
        dunce::canonicalize(&plan.build_directory)? == build_directory,
        "package build directory does not match the fixed product and architecture path"
    );
    Ok(build_directory)
}

pub(super) fn create_package_output(repository: &Path, path: &Path) -> Result<PathBuf> {
    ensure!(path.is_absolute(), "package output path must be absolute");
    let mut current = PathBuf::new();
    for component in path.components() {
        match component {
            std::path::Component::Prefix(prefix) => current.push(prefix.as_os_str()),
            std::path::Component::RootDir => current.push(component.as_os_str()),
            std::path::Component::CurDir => {}
            std::path::Component::ParentDir => {
                anyhow::bail!("package output path is not normalized")
            }
            std::path::Component::Normal(name) => {
                current.push(name);
                match fs::symlink_metadata(&current) {
                    Ok(metadata) => ensure!(
                        metadata.is_dir() && !metadata.file_type().is_symlink(),
                        "package output path contains a non-directory or linked component: {}",
                        current.display()
                    ),
                    Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                        fs::create_dir(&current).with_context(|| {
                            format!(
                                "could not create package output directory {}",
                                current.display()
                            )
                        })?;
                    }
                    Err(error) => {
                        return Err(error).context("could not inspect package output path");
                    }
                }
            }
        }
    }
    let output = dunce::canonicalize(path).context("could not resolve package output directory")?;
    let metadata =
        fs::symlink_metadata(path).context("could not inspect package output directory")?;
    ensure!(
        metadata.is_dir() && !metadata.file_type().is_symlink(),
        "package output must be a regular directory"
    );
    ensure!(
        !output.starts_with(repository),
        "package output must be outside the source checkout"
    );
    ensure!(
        fs::read_dir(&output)?.next().is_none(),
        "package output directory must be empty"
    );
    Ok(output)
}

pub(super) fn canonical_report_path(output: &Path, report: &Path) -> Result<PathBuf> {
    let parent = report
        .parent()
        .context("package report path has no parent")?;
    let parent = dunce::canonicalize(parent).with_context(|| {
        format!(
            "could not resolve package report directory {}",
            parent.display()
        )
    })?;
    ensure!(
        parent == output,
        "package report must be written directly inside package output"
    );
    ensure!(
        report
            .file_name()
            .is_some_and(|name| name == "package-report.json"),
        "package report filename must be package-report.json"
    );
    let metadata = match fs::symlink_metadata(report) {
        Ok(metadata) => Some(metadata),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
        Err(error) => return Err(error).context("could not inspect package report destination"),
    };
    if let Some(metadata) = metadata {
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "package report destination must be a regular file"
        );
        ensure!(
            metadata.len() <= MAX_REPORT_BYTES,
            "existing package report exceeds its size limit"
        );
    }
    Ok(parent.join("package-report.json"))
}
