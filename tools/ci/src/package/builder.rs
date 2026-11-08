//! Package CLI admission and archive orchestration.

mod inputs;
mod paths;

use std::collections::BTreeMap;
use std::fs;
use std::num::NonZeroUsize;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use clap::Args;

use crate::model::{Architecture, ExecutionContext, Product};
use crate::process::{CommandSpec, Runner, run_checked};
use crate::source::{identify_source, project_version};
use crate::tooling::{Toolset, load_toolset, write_json_report};

use super::archive;
use super::consumers;
use super::dependencies;
use super::inspect::{inspect_package_archive, run_plugin_package_probe};
use super::{
    ConsumerEvidence, PackageExpectation, PackageKind, PackageMode, PackageReport,
    PackageReportEntry, PluginProbeEvidence, VerifiedPackage,
};

const PACKAGE_TIMEOUT: Duration = Duration::from_mins(30);
const PACKAGE_OUTPUT_LIMIT: usize = 32 * 1024 * 1024;
const MAX_REPORT_BYTES: u64 = 16 * 1024 * 1024;
#[derive(Debug, Args)]
pub struct PackageArgs {
    #[arg(long, default_value = ".")]
    root: PathBuf,
    #[arg(long)]
    toolset: PathBuf,
    #[arg(long, value_enum)]
    product: Product,
    #[arg(long, value_enum)]
    architecture: Architecture,
    #[arg(long)]
    build_directory: PathBuf,
    #[arg(long)]
    output: PathBuf,
    #[arg(long)]
    report: PathBuf,
    #[arg(long, value_enum, default_value = "development")]
    mode: PackageMode,
    #[arg(long)]
    jobs: Option<NonZeroUsize>,
}

impl PackageArgs {
    /// Builds packages from one already configured product build directory.
    ///
    /// # Errors
    ///
    /// Returns an error if the build, toolset, source identity, package payload, or report
    /// destination fails validation or package creation.
    pub fn run(self, runner: &mut dyn Runner) -> Result<()> {
        ensure!(
            self.build_directory.is_absolute(),
            "package build directory must be absolute"
        );
        ensure!(
            self.output.is_absolute(),
            "package output directory must be absolute"
        );
        ensure!(
            self.report.is_absolute(),
            "package report path must be absolute"
        );
        let repository = dunce::canonicalize(&self.root).with_context(|| {
            format!("could not resolve repository root {}", self.root.display())
        })?;
        ensure!(repository.is_dir(), "repository root is not a directory");
        let output = paths::create_package_output(&repository, &self.output)?;
        let report_path = paths::canonical_report_path(&output, &self.report)?;
        ensure!(
            !report_path.exists(),
            "package report destination already exists: {}",
            report_path.display()
        );

        let toolset = load_toolset(&self.toolset)?;
        ensure!(
            toolset.platform() == "windows-x64",
            "native package installation requires a Windows x64 toolset"
        );
        let source = identify_source(&repository, runner)?;
        let version = project_version(&repository, self.product)?.to_string();
        let jobs = self.jobs.unwrap_or_else(default_jobs);
        ensure!(jobs.get() <= 256, "jobs must be between 1 and 256");
        let build_directory = paths::validate_build_directory(
            &repository,
            &self.build_directory,
            self.product,
            self.architecture,
        )?;
        let native_output = build_directory
            .parent()
            .and_then(Path::parent)
            .context("package build directory is not beneath an output root")?;
        let native_output = dunce::canonicalize(native_output)
            .context("could not resolve native build output root")?;
        let context = ExecutionContext {
            repository: repository.clone(),
            output: native_output,
            jobs,
            toolset,
            compiler_report: None,
            source,
        };
        let report = build_package(
            self.product,
            self.architecture,
            &version,
            &build_directory,
            &context,
            &output,
            self.mode,
            runner,
        )?;
        write_json_report(&report_path, &report)?;
        println!(
            "Created {} package archive(s) for {} {}",
            report.packages.len(),
            report.product.as_str(),
            report.architecture.as_str()
        );
        Ok(())
    }
}

/// Creates and immediately audits all three package kinds for one product build.
///
/// # Errors
///
/// Returns an error if build evidence, release inputs, an install component, the source
/// dependency receipt, an archive payload, or a consumer probe fails validation.
#[allow(
    clippy::too_many_arguments,
    reason = "the stable package entry point keeps product, architecture, provenance, and execution inputs explicit"
)]
pub fn build_package(
    product: Product,
    architecture: Architecture,
    version: &str,
    build_directory: &Path,
    context: &ExecutionContext,
    package_output: &Path,
    mode: PackageMode,
    runner: &mut dyn Runner,
) -> Result<PackageReport> {
    inputs::validate_package_build_inputs(
        product,
        architecture,
        version,
        build_directory,
        context,
        mode,
    )?;
    let output = paths::create_package_output(&context.repository, package_output)?;
    let temporary = tempfile::TempDir::new_in(&output)
        .context("could not create isolated package staging directory")?;
    let builder = PackageBuilder {
        product,
        architecture,
        version,
        build_directory,
        context,
        package_output: &output,
        staging_root: temporary.path(),
        mode,
    };
    let mut packages = Vec::new();
    let mut consumer_checks = Vec::new();
    for kind in [
        PackageKind::Binary,
        PackageKind::Symbols,
        PackageKind::Source,
    ] {
        let mut package = builder.stage_kind(kind, runner)?;
        if let Some(consumer) = package.consumer_check.take() {
            consumer_checks.push(consumer);
        }
        packages.push(package);
    }
    let report = PackageReport {
        schema_version: 1,
        product,
        architecture,
        version: version.to_owned(),
        mode,
        source: context.source.clone(),
        packages: packages
            .iter()
            .map(|package| package.report.clone())
            .collect(),
        consumer_checks,
    };
    for package in packages {
        fs::rename(&package.staged_archive, &package.final_path).with_context(|| {
            format!(
                "could not publish package archive {}",
                package.final_path.display()
            )
        })?;
    }
    Ok(report)
}

struct PackageBuilder<'a> {
    product: Product,
    architecture: Architecture,
    version: &'a str,
    build_directory: &'a Path,
    context: &'a ExecutionContext,
    package_output: &'a Path,
    staging_root: &'a Path,
    mode: PackageMode,
}

struct StagedPackage {
    staged_archive: PathBuf,
    final_path: PathBuf,
    report: PackageReportEntry,
    consumer_check: Option<ConsumerEvidence>,
}

impl PackageBuilder<'_> {
    fn stage_kind(&self, kind: PackageKind, runner: &mut dyn Runner) -> Result<StagedPackage> {
        let stage = self.staging_root.join(kind.as_str());
        fs::create_dir(&stage).context("could not create package staging root")?;
        match kind {
            PackageKind::Binary | PackageKind::Symbols => install_component(
                self.product,
                kind,
                self.build_directory,
                &stage,
                &self.context.toolset,
                runner,
            )?,
            PackageKind::Source => dependencies::copy_source_bundle(
                self.product,
                &self.context.repository,
                self.build_directory,
                &stage,
                runner,
            )?,
        }
        let expectation = PackageExpectation {
            product: self.product,
            architecture: (kind != PackageKind::Source).then_some(self.architecture),
            version: self.version.to_owned(),
            kind,
            source: self.context.source.clone(),
        };
        let name = archive::package_filename(&expectation);
        let staged_archive = self.staging_root.join(&name);
        archive::write_package_archive(&stage, &staged_archive, &expectation)?;
        let verified =
            inspect_package_archive(&staged_archive, &expectation, self.context, runner)?;
        let size_bytes = fs::metadata(&staged_archive)?.len();
        let final_path = self.package_output.join(&name);
        ensure!(
            !final_path.exists(),
            "package archive destination already exists: {}",
            final_path.display()
        );

        let (plugin_probe, consumer_check) = self.verify_package_consumers(
            kind,
            &stage,
            &staged_archive,
            &expectation,
            &verified,
            runner,
        )?;

        Ok(StagedPackage {
            staged_archive,
            final_path,
            report: PackageReportEntry {
                kind,
                name,
                size_bytes,
                archive_sha256: verified.archive_sha256,
                manifest_sha256: verified.manifest_sha256,
                entry_count: verified.entry_count,
                plugin_probe,
            },
            consumer_check,
        })
    }

    fn verify_package_consumers(
        &self,
        kind: PackageKind,
        stage: &Path,
        staged_archive: &Path,
        expectation: &PackageExpectation,
        verified: &VerifiedPackage,
        runner: &mut dyn Runner,
    ) -> Result<(Option<PluginProbeEvidence>, Option<ConsumerEvidence>)> {
        let plugin_probe = if kind == PackageKind::Binary && self.product != Product::Foundation {
            Some(run_plugin_package_probe(
                staged_archive,
                verified,
                expectation,
                self.mode,
                self.context,
                runner,
            )?)
        } else {
            None
        };
        let consumer_work_root = self.staging_root.join("consumers");
        let consumer_check = if kind == PackageKind::Binary && self.product == Product::Foundation {
            Some(consumers::verify_foundation_installed_package(
                stage,
                &verified.archive_sha256,
                self.context,
                self.architecture,
                &consumer_work_root,
                runner,
            )?)
        } else if kind == PackageKind::Source {
            Some(consumers::verify_source_package_consumer(
                staged_archive,
                expectation,
                &verified.archive_sha256,
                consumers::SourceConsumerInputs {
                    context: self.context,
                    architecture: self.architecture,
                    native_build_directory: self.build_directory,
                    work_root: &consumer_work_root,
                },
                runner,
            )?)
        } else {
            None
        };
        Ok((plugin_probe, consumer_check))
    }
}

fn install_component(
    product: Product,
    kind: PackageKind,
    build_directory: &Path,
    stage: &Path,
    toolset: &Toolset,
    runner: &mut dyn Runner,
) -> Result<()> {
    let component = match (product, kind) {
        (Product::Foundation, PackageKind::Binary) => "InputNextAdaptersBinary",
        (Product::Foundation, PackageKind::Symbols) => "InputNextAdaptersSymbols",
        (Product::SkyrimSe, PackageKind::Binary) => "SkyrimAdapter",
        (Product::SkyrimSe, PackageKind::Symbols) => "SkyrimSymbols",
        (Product::SkyrimLe, PackageKind::Binary) => "SkyrimLEAdapter",
        (Product::SkyrimLe, PackageKind::Symbols) => "SkyrimLESymbols",
        (Product::Fallout4, PackageKind::Binary) => "Fallout4Adapter",
        (Product::Fallout4, PackageKind::Symbols) => "Fallout4Symbols",
        (_, PackageKind::Source) => {
            anyhow::bail!("source packages are not CMake install components")
        }
    };
    let cmake = toolset.executable("cmake")?;
    run_checked(
        runner,
        &CommandSpec {
            program: cmake.to_path_buf(),
            args: vec![
                "--install".into(),
                build_directory.as_os_str().to_owned(),
                "--component".into(),
                component.into(),
                "--prefix".into(),
                stage.as_os_str().to_owned(),
            ],
            cwd: build_directory.to_path_buf(),
            env: BTreeMap::default(),
            timeout: PACKAGE_TIMEOUT,
            output_limit: PACKAGE_OUTPUT_LIMIT,
        },
    )?;
    Ok(())
}

fn default_jobs() -> NonZeroUsize {
    std::thread::available_parallelism().unwrap_or(NonZeroUsize::MIN)
}
