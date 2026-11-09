//! Static final-job release evidence orchestration.

pub(super) mod evidence;
pub(super) mod handoff;

use std::collections::{BTreeMap, BTreeSet};
use std::num::NonZeroUsize;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result, ensure};
use serde::Serialize;
use tempfile::{TempDir, tempdir};

use crate::model::{Architecture, ExecutionContext, Product, SourceIdentity};
use crate::package::PackageReport;
use crate::process::Runner;
use crate::qualification::{
    QualificationMode, QualificationReport, RuntimePairOrigin, VerifiedRuntimePair,
    inspect_runtime_pair,
};
use crate::source::project_version;
use crate::tooling::{load_toolset, write_json_report};

use self::evidence::{
    add_verified_package, expected_handoff_files, parse_artifact_row, parse_native_report,
    validate_native_report, validate_package_report, validate_packages,
    validate_qualification_report,
};
use self::handoff::{
    MAX_REPORT_BYTES, MAX_RUNTIME_MANIFEST_BYTES, TagSourceTree, inspect_handoff_row,
    materialize_tag_source, read_bounded_regular_file, read_runtime_manifest,
    validate_handoff_directories,
};
use super::DraftArgs;
use super::VerifyArgs;
use super::identity::{parse_architecture, parse_product};

#[derive(Clone, Copy, Debug)]
pub(super) struct ReleasePaths<'a> {
    workflow_root: &'a Path,
    git: &'a Path,
    tag: &'a str,
    toolset: &'a Path,
    receipt: &'a Path,
    handoff: &'a Path,
    runtime_manifest: &'a Path,
}

impl<'a> ReleasePaths<'a> {
    pub(super) fn for_verify(args: &'a VerifyArgs) -> Self {
        Self {
            workflow_root: &args.root,
            git: &args.git,
            tag: &args.tag,
            toolset: &args.toolset,
            receipt: &args.receipt,
            handoff: &args.handoff,
            runtime_manifest: &args.runtime_manifest,
        }
    }

    pub(super) fn for_draft(args: &'a DraftArgs) -> Self {
        Self {
            workflow_root: &args.root,
            git: &args.git,
            tag: &args.tag,
            toolset: &args.toolset,
            receipt: &args.receipt,
            handoff: &args.handoff,
            runtime_manifest: &args.runtime_manifest,
        }
    }
}

pub(super) fn materialize_tag_source_to(
    workflow_root: &Path,
    git: &Path,
    source_commit: &str,
    output: &Path,
    runner: &mut dyn Runner,
) -> Result<PathBuf> {
    handoff::materialize_tag_source_to(workflow_root, git, source_commit, output, runner)
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub(super) struct VerifiedAsset {
    pub name: String,
    pub sha256: String,
    pub size_bytes: u64,
    pub artifact_id: u64,
}

#[derive(Clone, Debug)]
pub(super) struct AdmittedAsset {
    pub asset: VerifiedAsset,
    pub path: PathBuf,
    pub manifest_sha256: String,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub(super) struct ReleaseReport {
    pub schema_version: u32,
    pub state: String,
    pub repository: String,
    pub product: Product,
    pub version: String,
    pub tag: String,
    pub source_commit: String,
    pub workflow_commit: String,
    pub run_id: String,
    pub run_attempt: String,
    pub runtime_version: String,
    pub runtime_repository: Option<String>,
    pub runtime_release_id: Option<u64>,
    pub runtime_tag: Option<String>,
    pub runtime_core_sha256: String,
    pub runtime_effects_sha256: String,
    pub runtime_x64_sha256: String,
    pub runtime_x86_sha256: String,
    pub assets: Vec<VerifiedAsset>,
    pub draft_id: Option<u64>,
    pub draft_url: Option<String>,
    pub uploaded_assets: Vec<String>,
    pub failure: Option<String>,
}

#[derive(Debug)]
pub(super) struct VerifiedRelease {
    pub identity: super::identity::ReleaseIdentity,
    pub runtime_pair: VerifiedRuntimePair,
    pub assets: Vec<VerifiedAsset>,
    pub asset_paths: Vec<PathBuf>,
    _tag_source: TagSourceTree,
    _output: TempDir,
}

impl VerifiedRelease {
    pub(super) fn report(&self, state: &str) -> ReleaseReport {
        ReleaseReport {
            schema_version: 1,
            state: state.to_owned(),
            repository: self.identity.repository.clone(),
            product: self.identity.product,
            version: self.identity.version.clone(),
            tag: self.identity.tag.clone(),
            source_commit: self.identity.source_commit.clone(),
            workflow_commit: self.identity.workflow_commit.clone(),
            run_id: self.identity.run_id.clone(),
            run_attempt: self.identity.run_attempt.clone(),
            runtime_version: self.runtime_pair.version.clone(),
            runtime_repository: self.runtime_pair.repository.clone(),
            runtime_release_id: self.runtime_pair.release_id,
            runtime_tag: self.runtime_pair.tag.clone(),
            runtime_core_sha256: self.runtime_pair.core_sha256.clone(),
            runtime_effects_sha256: self.runtime_pair.effects_sha256.clone(),
            runtime_x64_sha256: self.runtime_pair.x64.sha256.clone(),
            runtime_x86_sha256: self.runtime_pair.x86.sha256.clone(),
            assets: self.assets.clone(),
            draft_id: None,
            draft_url: None,
            uploaded_assets: Vec::new(),
            failure: None,
        }
    }
}

#[cfg(test)]
mod tests;

pub(super) fn run(args: &VerifyArgs, runner: &mut dyn Runner) -> Result<()> {
    let verified = prepare_release(ReleasePaths::for_verify(args), runner)?;
    if let Some(path) = &args.report {
        super::validate_output_path(&args.root, path)?;
        write_json_report(path, &verified.report("verified"))?;
    }
    println!(
        "verified {} release assets for {} {}",
        verified.assets.len(),
        verified.identity.product.as_str(),
        verified.identity.version
    );
    Ok(())
}

#[allow(
    clippy::too_many_lines,
    reason = "final release admission keeps source, workflow, runtime, and handoff checks in one ordered sequence"
)]
pub(super) fn prepare_release(
    paths: ReleasePaths<'_>,
    runner: &mut dyn Runner,
) -> Result<VerifiedRelease> {
    let ReleasePaths {
        workflow_root,
        git,
        tag,
        toolset: toolset_path,
        receipt: receipt_path,
        handoff: handoff_path,
        runtime_manifest: runtime_manifest_path,
    } = paths;
    let context = super::identity::WorkflowContext::from_environment()?;
    context.validate()?;
    validate_final_job_authority()?;
    let identity = super::identity::qualify(&context, workflow_root, git, tag, runner)?;
    ensure!(
        runtime_manifest_path.is_absolute(),
        "runtime manifest path must be absolute"
    );

    let toolset = load_toolset(toolset_path)?;
    super::artifacts::validate_remote_tag(&context, &identity, &toolset, runner)?;
    let receipt =
        super::artifacts::verify_receipt(receipt_path, &context, &identity, &toolset, runner)?;
    let expected_directories = receipt
        .artifacts
        .iter()
        .map(|artifact| artifact.name.clone())
        .collect::<BTreeSet<_>>();
    let handoff = validate_handoff_directories(handoff_path, &expected_directories)?;
    let tag_source = materialize_tag_source(workflow_root, git, &identity.source_commit, runner)?;
    let source = SourceIdentity {
        commit: Some(identity.source_commit.clone()),
        modified: false,
        snapshot_sha256: None,
    };
    let output = tempdir().context("create final static verification output")?;
    let runtime_manifest =
        dunce::canonicalize(runtime_manifest_path).context("resolve trusted runtime manifest")?;
    let runtime_bytes = read_runtime_manifest(&runtime_manifest)?;
    crate::json::parse_slice(&runtime_bytes)
        .context("published runtime manifest is not strict duplicate-free JSON")?;
    let context = ExecutionContext {
        repository: tag_source.root().to_path_buf(),
        output: dunce::canonicalize(output.path()).context("resolve final verification output")?,
        jobs: NonZeroUsize::new(16).context("fixed release job count is invalid")?,
        toolset,
        compiler_report: None,
        source: source.clone(),
    };
    let runtime_pair = inspect_runtime_pair(
        &runtime_manifest,
        tag_source.root(),
        &context,
        QualificationMode::Release,
        runner,
    )?;
    ensure!(
        runtime_pair.origin == RuntimePairOrigin::Published,
        "release verification requires a published runtime pair"
    );
    ensure!(
        runtime_pair.schema_version == 1,
        "unsupported verified runtime-pair schema"
    );
    ensure!(
        runtime_pair.x64.architecture == Architecture::X64,
        "published runtime x64 archive has the wrong architecture"
    );
    ensure!(
        runtime_pair.x86.architecture == Architecture::X86,
        "published runtime x86 archive has the wrong architecture"
    );

    let expected_assets = verify_matrix_handoffs(
        MatrixAdmission {
            workflow_root,
            handoff: &handoff,
            receipt: &receipt,
            identity: &identity,
            source: &source,
            runtime_bytes: &runtime_bytes,
            runtime_pair: &runtime_pair,
            context: &context,
            tag_source: tag_source.root(),
        },
        runner,
    )?;

    let assets = expected_assets
        .values()
        .map(|admitted| admitted.asset.clone())
        .collect::<Vec<_>>();
    let asset_paths = expected_assets
        .values()
        .map(|admitted| admitted.path.clone())
        .collect::<Vec<_>>();
    ensure!(
        !assets.is_empty(),
        "release verification found no package assets"
    );
    Ok(VerifiedRelease {
        identity,
        runtime_pair,
        assets,
        asset_paths,
        _tag_source: tag_source,
        _output: output,
    })
}

#[derive(Clone, Copy, Debug)]
struct MatrixAdmission<'a> {
    workflow_root: &'a Path,
    handoff: &'a Path,
    receipt: &'a super::artifacts::ArtifactReceipt,
    identity: &'a super::identity::ReleaseIdentity,
    source: &'a SourceIdentity,
    runtime_bytes: &'a [u8],
    runtime_pair: &'a VerifiedRuntimePair,
    context: &'a ExecutionContext,
    tag_source: &'a Path,
}

fn verify_matrix_handoffs(
    admission: MatrixAdmission<'_>,
    runner: &mut dyn Runner,
) -> Result<BTreeMap<String, AdmittedAsset>> {
    let MatrixAdmission {
        workflow_root: _,
        handoff,
        receipt,
        identity,
        source,
        runtime_bytes,
        runtime_pair,
        context,
        tag_source,
    } = admission;
    let mut expected_assets = BTreeMap::new();
    let mut artifact_by_row = BTreeMap::new();
    let attestation_artifact_name = super::artifacts::attestation_artifact_name(
        identity.product,
        &identity.run_id,
        &identity.run_attempt,
    );
    for artifact in &receipt.artifacts {
        if artifact.name == attestation_artifact_name {
            continue;
        }
        let row = parse_artifact_row(&artifact.name, identity)?;
        ensure!(
            artifact_by_row.insert(row, artifact).is_none(),
            "artifact receipt contains a duplicate product/architecture row"
        );
    }
    for (product_name, architecture_name) in super::identity::required_rows(identity.product) {
        let product = parse_product(product_name)?;
        let architecture = parse_architecture(architecture_name)?;
        let artifact_name = format!(
            "release-{product_name}-{architecture_name}-{}-{}",
            identity.run_id, identity.run_attempt
        );
        let artifact = artifact_by_row
            .get(&(product, architecture))
            .with_context(|| {
                format!("artifact receipt lacks {product_name} {architecture_name}")
            })?;
        ensure!(
            artifact.name == artifact_name,
            "artifact receipt row name is inconsistent"
        );
        let has_packages = product == identity.product;
        let expected_files =
            expected_handoff_files(product, architecture, &identity.version, has_packages);
        let files = inspect_handoff_row(handoff, &artifact_name, &expected_files)?;
        let row_manifest = read_bounded_regular_file(
            &files["runtime-manifest.json"],
            MAX_RUNTIME_MANIFEST_BYTES,
            "row runtime manifest",
        )?;
        ensure!(
            row_manifest == runtime_bytes,
            "matrix artifact runtime manifest differs from the trusted dispatch input"
        );
        validate_row_qualification(
            &files["qualification-report.json"],
            product,
            architecture,
            source,
            runtime_pair,
        )?;

        if has_packages {
            let product_version = project_version(tag_source, product)?.to_string();
            let verified = validate_row_packages(
                &files,
                product,
                architecture,
                &product_version,
                source,
                context,
                runner,
            )?;
            for package in verified {
                add_verified_package(&mut expected_assets, package, artifact.id)?;
            }
        }
    }

    verify_attestation_artifact(
        admission,
        &attestation_artifact_name,
        &mut expected_assets,
        runner,
    )?;

    Ok(expected_assets)
}

fn verify_attestation_artifact(
    admission: MatrixAdmission<'_>,
    attestation_artifact_name: &str,
    expected_assets: &mut BTreeMap<String, AdmittedAsset>,
    runner: &mut dyn Runner,
) -> Result<()> {
    let MatrixAdmission {
        workflow_root,
        handoff,
        receipt,
        identity,
        context,
        tag_source,
        ..
    } = admission;
    let attestation_artifact = receipt
        .artifacts
        .iter()
        .find(|artifact| artifact.name == attestation_artifact_name)
        .with_context(|| format!("artifact receipt lacks {attestation_artifact_name}"))?;
    let bundle_name =
        super::attest::provenance_bundle_filename(identity.product, &identity.version);
    let expected_bundle_files = BTreeSet::from([bundle_name.clone()]);
    let bundle_files =
        inspect_handoff_row(handoff, attestation_artifact_name, &expected_bundle_files)?;
    let bundle_path = &bundle_files[&bundle_name];
    let trust = super::attest::ReleaseTrust::load(workflow_root, identity)?;
    let expected_subjects = expected_assets
        .iter()
        .map(|(name, admitted)| (name.clone(), admitted.asset.sha256.clone()))
        .collect::<BTreeMap<_, _>>();
    let (_primary_name, primary_admitted) = expected_assets
        .iter()
        .next()
        .context("no release packages were provided for provenance verification")?;
    let primary_subject_path = &primary_admitted.path;
    let gh = context.toolset.executable("gh")?;
    super::attest::verify_provenance(
        runner,
        &super::attest::ProvenanceVerification {
            gh,
            cwd: tag_source,
            primary_subject_path,
            bundle_path,
            expected_subjects: &expected_subjects,
            identity,
            trust: &trust,
        },
    )?;

    let bundle_bytes = read_bounded_regular_file(
        bundle_path,
        super::attest::MAX_GH_OUTPUT_BYTES as u64,
        "attestation bundle",
    )?;
    let bundle_sha256 = crate::tooling::hash_file(bundle_path)?;
    let bundle_asset = VerifiedAsset {
        name: bundle_name.clone(),
        sha256: bundle_sha256,
        size_bytes: u64::try_from(bundle_bytes.len())?,
        artifact_id: attestation_artifact.id,
    };
    expected_assets.insert(
        bundle_name,
        AdmittedAsset {
            asset: bundle_asset,
            path: bundle_path.clone(),
            manifest_sha256: String::new(),
        },
    );
    Ok(())
}

fn validate_row_qualification(
    path: &Path,
    product: Product,
    architecture: Architecture,
    source: &SourceIdentity,
    runtime_pair: &VerifiedRuntimePair,
) -> Result<()> {
    let bytes = read_bounded_regular_file(path, MAX_REPORT_BYTES, "qualification report")?;
    let report: QualificationReport = crate::json::parse(&bytes)
        .context("qualification report has an invalid or duplicate-key schema")?;
    validate_qualification_report(&report, product, architecture, source, runtime_pair)
}

fn validate_row_packages(
    files: &BTreeMap<String, PathBuf>,
    product: Product,
    architecture: Architecture,
    version: &str,
    source: &SourceIdentity,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<Vec<crate::package::VerifiedPackage>> {
    let native = parse_native_report(&files["native-report.json"])?;
    validate_native_report(&native, product, architecture, source)?;
    let package_bytes = read_bounded_regular_file(
        &files["package-report.json"],
        MAX_REPORT_BYTES,
        "package report",
    )?;
    let package_report: PackageReport = crate::json::parse(&package_bytes)
        .context("package report has an invalid or duplicate-key schema")?;
    let package_report_value: serde_json::Value = crate::json::parse_slice(&package_bytes)
        .context("package report has invalid or duplicate-key JSON")?;
    validate_package_report(
        &package_report,
        &package_report_value,
        product,
        architecture,
        version,
        source,
    )?;
    validate_packages(
        files,
        &package_report,
        product,
        architecture,
        version,
        source,
        context,
        runner,
    )
}

fn validate_final_job_authority() -> Result<()> {
    for name in [
        "ADAPTERS_QUALIFY_RESULT",
        "ADAPTERS_QUALITY_RESULT",
        "ADAPTERS_PACKAGE_RESULT",
        "ADAPTERS_ATTEST_RESULT",
    ] {
        let value = std::env::var(name)
            .with_context(|| format!("trusted workflow dependency result {name} is missing"))?;
        ensure!(
            value == "success",
            "trusted workflow dependency {name} did not succeed"
        );
    }
    Ok(())
}
