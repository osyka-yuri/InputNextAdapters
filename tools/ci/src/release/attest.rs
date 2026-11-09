//! GitHub CLI cryptographic verification plus exact statement admission.
//!
//! # Trust Boundary
//!
//! Package archives are built on Windows matrix runners without OIDC credentials,
//! then inspected and signed in a separate Linux attestation runner using GitHub Artifact
//! Attestations (`actions/attest`).
//!
//! The resulting Sigstore SLSA Provenance v1 bundle cryptographically certifies that the
//! archives were admitted and signed within the trusted GitHub Actions release workflow on a
//! GitHub-hosted runner. The link between intermediate native build outputs and the signed
//! bundle is enforced by host-side deterministic admission policies (package report validation,
//! strict source commit checks, hash verification, and run artifact inventory audit), rather
//! than in-compiler cryptographic tracing.

use std::collections::BTreeMap;
use std::ffi::OsString;
use std::path::Path;
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use serde::Deserialize;
use serde_json::Value;

use crate::model::Product;
use crate::process::{CommandSpec, Runner, run_checked};

use super::identity::ReleaseIdentity;
use super::verify::handoff::read_bounded_regular_file;

pub(super) const EXPECTED_GH_VERSION: &str = "2.103.3";
pub(super) const MAX_GH_OUTPUT_BYTES: usize = 32 * 1024 * 1024;
pub(super) const COMMAND_TIMEOUT: Duration = Duration::from_secs(120);
pub(super) const SLSA_PROVENANCE_PREDICATE: &str = "https://slsa.dev/provenance/v1";

#[must_use]
pub(super) fn provenance_bundle_filename(product: Product, version: &str) -> String {
    format!(
        "InputNextAdapters-{}-{version}-provenance.sigstore.json",
        product.as_str()
    )
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub(super) struct ReleaseTrust {
    pub schema_version: u32,
    pub repository: String,
    pub repository_id: u64,
    pub repository_owner_id: u64,
}

impl ReleaseTrust {
    pub(super) fn load(source_root: &Path, identity: &ReleaseIdentity) -> Result<Self> {
        let path = source_root.join(".github/ci/release-trust.json");
        let bytes = read_bounded_regular_file(&path, 16 * 1024, "release trust policy")?;
        let trust: Self = crate::json::parse(&bytes).context("release trust policy is invalid")?;
        trust.validate(identity)?;
        Ok(trust)
    }

    pub(super) fn validate(&self, identity: &ReleaseIdentity) -> Result<()> {
        ensure!(
            self.schema_version == 1,
            "unsupported release trust policy schema"
        );
        ensure!(
            self.repository == identity.repository,
            "release trust policy repository differs from qualified identity"
        );
        ensure!(
            self.repository_id != 0 && self.repository_owner_id != 0,
            "release trust policy repository and owner IDs must be nonzero"
        );
        Ok(())
    }
}

pub(super) struct ProvenanceVerification<'a> {
    pub gh: &'a Path,
    pub cwd: &'a Path,
    pub primary_subject_path: &'a Path,
    pub bundle_path: &'a Path,
    pub expected_subjects: &'a BTreeMap<String, String>,
    pub identity: &'a ReleaseIdentity,
    pub trust: &'a ReleaseTrust,
}

pub(super) fn verify_provenance(
    runner: &mut dyn Runner,
    verification: &ProvenanceVerification<'_>,
) -> Result<()> {
    let ProvenanceVerification {
        gh,
        cwd,
        primary_subject_path,
        bundle_path,
        expected_subjects,
        identity,
        trust,
    } = verification;
    validate_gh_version(runner, gh, cwd)?;
    let subject_metadata = std::fs::symlink_metadata(primary_subject_path).with_context(|| {
        format!(
            "could not inspect primary subject archive {}",
            primary_subject_path.display()
        )
    })?;
    ensure!(
        subject_metadata.is_file() && !subject_metadata.file_type().is_symlink(),
        "primary subject archive must be a regular non-symlink file: {}",
        primary_subject_path.display()
    );
    let first_subject_name = primary_subject_path
        .file_name()
        .and_then(|name| name.to_str())
        .context("primary subject archive path has no valid filename")?;
    let expected_hash = expected_subjects.get(first_subject_name).with_context(|| {
        format!("primary subject '{first_subject_name}' is not in expected subjects")
    })?;
    let actual_hash = crate::tooling::hash_file(primary_subject_path)?;
    ensure!(
        actual_hash == *expected_hash,
        "primary subject archive hash mismatch for {first_subject_name}"
    );
    let output = verify_attestation(
        runner,
        gh,
        cwd,
        identity,
        primary_subject_path,
        bundle_path,
        SLSA_PROVENANCE_PREDICATE,
    )?;
    let (verification_res, statement) = exact_verified_statement(&output)?;
    ensure!(
        statement["predicateType"].as_str() == Some(SLSA_PROVENANCE_PREDICATE),
        "verified provenance predicate type is incorrect"
    );
    validate_subjects(&statement["subject"], expected_subjects)?;
    validate_certificate(verification_res, identity, trust)?;
    Ok(())
}

fn validate_gh_version(runner: &mut dyn Runner, gh: &Path, cwd: &Path) -> Result<()> {
    let output = run_checked(
        runner,
        &command_spec(gh, cwd, ["--version"], Duration::from_secs(30)),
    )?;
    let text =
        std::str::from_utf8(&output.stdout).context("GitHub CLI version output is not UTF-8")?;
    let first_line = text
        .lines()
        .next()
        .context("GitHub CLI version output is empty")?;
    ensure!(
        first_line.split_whitespace().take(3).collect::<Vec<_>>()
            == ["gh", "version", EXPECTED_GH_VERSION],
        "GitHub CLI version must be exactly {EXPECTED_GH_VERSION}"
    );
    Ok(())
}

fn verify_attestation(
    runner: &mut dyn Runner,
    gh: &Path,
    cwd: &Path,
    identity: &ReleaseIdentity,
    subject: &Path,
    bundle: &Path,
    predicate_type: &str,
) -> Result<Value> {
    let signer_workflow = format!("{}/.github/workflows/release.yml", identity.repository);
    let args = vec![
        OsString::from("attestation"),
        OsString::from("verify"),
        subject.as_os_str().to_os_string(),
        OsString::from("--repo"),
        OsString::from(&identity.repository),
        OsString::from("--bundle"),
        bundle.as_os_str().to_os_string(),
        OsString::from("--predicate-type"),
        OsString::from(predicate_type),
        OsString::from("--signer-workflow"),
        OsString::from(signer_workflow),
        OsString::from("--signer-digest"),
        OsString::from(&identity.workflow_commit),
        OsString::from("--source-digest"),
        OsString::from(&identity.workflow_commit),
        OsString::from("--source-ref"),
        OsString::from("refs/heads/main"),
        OsString::from("--deny-self-hosted-runners"),
        OsString::from("--format"),
        OsString::from("json"),
    ];
    let output = run_checked(runner, &command_spec(gh, cwd, args, COMMAND_TIMEOUT))?;
    crate::json::parse_slice(&output.stdout)
        .context("GitHub CLI attestation output is invalid JSON")
}

pub(super) fn exact_verified_statement(output: &Value) -> Result<(&Value, &Value)> {
    let results = if let Some(values) = output.as_array() {
        values.as_slice()
    } else {
        std::slice::from_ref(output)
    };
    ensure!(
        results.len() == 1,
        "attestation verification must return exactly one statement"
    );
    let verification = &results[0]["verificationResult"];
    ensure!(
        verification["signature"]["certificate"].is_object(),
        "attestation output has no verified signing certificate"
    );
    let statement = verification
        .get("statement")
        .context("attestation output has no verified statement")?;
    ensure!(statement.is_object(), "verified statement is not an object");
    Ok((verification, statement))
}

pub(super) fn validate_subjects(
    subject_value: &Value,
    expected: &BTreeMap<String, String>,
) -> Result<()> {
    let subjects = subject_value
        .as_array()
        .context("verified statement has no subject array")?;
    ensure!(
        subjects.len() == expected.len(),
        "verified attestation subject count is not exact"
    );
    let mut actual = BTreeMap::new();
    for subject in subjects {
        let name = subject["name"]
            .as_str()
            .context("verified subject name is missing")?;
        let digest = subject["digest"]
            .as_object()
            .context("verified subject digest is missing")?;
        ensure!(
            digest.len() == 1,
            "verified subject digest has unexpected algorithms"
        );
        let sha256 = digest["sha256"]
            .as_str()
            .context("verified subject SHA-256 is missing")?;
        ensure!(
            actual.insert(name.to_owned(), sha256.to_owned()).is_none(),
            "verified attestation has duplicate subjects"
        );
    }
    ensure!(
        actual == *expected,
        "verified attestation subjects do not exactly match the admitted assets"
    );
    Ok(())
}

pub(super) fn validate_certificate(
    verification: &Value,
    identity: &ReleaseIdentity,
    trust: &ReleaseTrust,
) -> Result<()> {
    let certificate = verification["signature"]["certificate"]
        .as_object()
        .context("verified attestation certificate is missing")?;
    let repo_uri = format!("https://github.com/{}", identity.repository);
    let signer = format!(
        "https://github.com/{}/.github/workflows/release.yml@refs/heads/main",
        identity.repository
    );
    let run_uri = format!(
        "https://github.com/{}/actions/runs/{}/attempts/{}",
        identity.repository, identity.run_id, identity.run_attempt
    );
    ensure!(
        certificate
            .get("sourceRepositoryURI")
            .and_then(Value::as_str)
            == Some(repo_uri.as_str())
            && certificate
                .get("sourceRepositoryDigest")
                .and_then(Value::as_str)
                == Some(identity.workflow_commit.as_str())
            && certificate
                .get("sourceRepositoryRef")
                .and_then(Value::as_str)
                == Some("refs/heads/main")
            && certificate.get("buildSignerURI").and_then(Value::as_str) == Some(signer.as_str())
            && certificate.get("buildSignerDigest").and_then(Value::as_str)
                == Some(identity.workflow_commit.as_str())
            && certificate.get("runInvocationURI").and_then(Value::as_str)
                == Some(run_uri.as_str())
            && certificate.get("runnerEnvironment").and_then(Value::as_str)
                == Some("github-hosted"),
        "attestation certificate source, signer, runner, or run identity differs from workflow context"
    );
    let repository_id = trust.repository_id.to_string();
    let owner_id = trust.repository_owner_id.to_string();
    ensure!(
        certificate
            .get("sourceRepositoryIdentifier")
            .and_then(Value::as_str)
            == Some(repository_id.as_str())
            && certificate
                .get("sourceRepositoryOwnerIdentifier")
                .and_then(Value::as_str)
                == Some(owner_id.as_str()),
        "attestation certificate repository or owner ID differs from trusted policy"
    );
    Ok(())
}

fn command_spec<I, S>(program: &Path, cwd: &Path, args: I, timeout: Duration) -> CommandSpec
where
    I: IntoIterator<Item = S>,
    S: Into<OsString>,
{
    CommandSpec {
        program: program.to_path_buf(),
        args: args.into_iter().map(Into::into).collect(),
        cwd: cwd.to_path_buf(),
        env: BTreeMap::default(),
        timeout,
        output_limit: MAX_GH_OUTPUT_BYTES,
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;

    use anyhow::{Context, Result};
    use serde_json::{Value, json};

    use crate::model::Product;
    use crate::release::identity::ReleaseIdentity;

    use super::{
        ReleaseTrust, SLSA_PROVENANCE_PREDICATE, exact_verified_statement, validate_certificate,
        validate_subjects,
    };

    /// Loads a synthetic GitHub CLI 2.103.3 verification fixture matching the schema format
    /// returned by `gh attestation verify --format json`.
    ///
    /// NOTE: This fixture tests offline parser and policy admission rules (`exact_verified_statement`,
    /// `validate_certificate`, and `validate_subjects`). Live end-to-end cryptographic verification
    /// of the Sigstore bundle and Fulcio certificates is performed by `gh attestation verify` in CI.
    fn synthetic_provenance_fixture() -> Result<(Value, ReleaseIdentity, ReleaseTrust)> {
        let output = serde_json::from_str(include_str!(
            "../../tests/release/gh-2.103.3-provenance.json"
        ))?;
        let identity = ReleaseIdentity {
            schema_version: 1,
            repository: "osyka-yuri/InputNextAdapters".to_owned(),
            product: Product::Fallout4,
            version: "0.1.0".to_owned(),
            tag: "fallout4/v0.1.0".to_owned(),
            source_commit: "9ee66aee83b237655ceab34b84a3bfc186687379".to_owned(),
            workflow_commit: "9ee66aee83b237655ceab34b84a3bfc186687379".to_owned(),
            run_id: "42600000000".to_owned(),
            run_attempt: "1".to_owned(),
            notes: "Initial Fallout 4 adapter release.".to_owned(),
        };
        let trust: ReleaseTrust = serde_json::from_value(json!({
            "schemaVersion": 1,
            "repository": "osyka-yuri/InputNextAdapters",
            "repositoryId": 1_451_824_097,
            "repositoryOwnerId": 51_163_508
        }))?;
        trust.validate(&identity)?;
        Ok((output, identity, trust))
    }

    #[test]
    fn accepts_synthetic_github_cli_verification_output() -> Result<()> {
        let (output, identity, trust) = synthetic_provenance_fixture()?;
        let (verification, statement) = exact_verified_statement(&output)?;
        validate_certificate(verification, &identity, &trust)?;
        assert_eq!(statement["predicateType"], SLSA_PROVENANCE_PREDICATE);
        let expected = BTreeMap::from([
            (
                "InputNextAdapters-fallout4-0.1.0-windows-x64.zip".to_owned(),
                "a1b2c3d4e5f60102030405060708090a1b2c3d4e5f60102030405060708090aa".to_owned(),
            ),
            (
                "InputNextAdapters-fallout4-0.1.0-windows-x64-symbols.zip".to_owned(),
                "b1b2c3d4e5f60102030405060708090a1b2c3d4e5f60102030405060708090bb".to_owned(),
            ),
            (
                "InputNextAdapters-fallout4-0.1.0-source.zip".to_owned(),
                "c1b2c3d4e5f60102030405060708090a1b2c3d4e5f60102030405060708090cc".to_owned(),
            ),
        ]);
        validate_subjects(&statement["subject"], &expected)?;

        let wrong_repo_trust: ReleaseTrust = serde_json::from_value(json!({
            "schemaVersion": 1,
            "repository": "osyka-yuri/OtherRepo",
            "repositoryId": 1_451_824_097,
            "repositoryOwnerId": 51_163_508
        }))?;
        assert!(wrong_repo_trust.validate(&identity).is_err());
        Ok(())
    }

    #[test]
    fn synthetic_certificate_rejects_invalid_or_missing_identity_fields() -> Result<()> {
        let (output, identity, trust) = synthetic_provenance_fixture()?;
        let (verification, _) = exact_verified_statement(&output)?;
        for field in [
            "sourceRepositoryURI",
            "sourceRepositoryIdentifier",
            "sourceRepositoryOwnerIdentifier",
            "sourceRepositoryDigest",
            "sourceRepositoryRef",
            "buildSignerURI",
            "buildSignerDigest",
            "runInvocationURI",
            "runnerEnvironment",
        ] {
            for invalid in [
                None,
                Some(Value::Null),
                Some(json!(42)),
                Some(json!("wrong")),
                Some(json!({
                    "sha1": verification["signature"]["certificate"][field]
                })),
            ] {
                let mut changed = verification.clone();
                let certificate = changed["signature"]["certificate"]
                    .as_object_mut()
                    .context("captured certificate is missing")?;
                if let Some(value) = invalid {
                    certificate.insert(field.to_owned(), value);
                } else {
                    certificate.remove(field);
                }
                assert!(
                    validate_certificate(&changed, &identity, &trust).is_err(),
                    "accepted invalid or missing {field}"
                );
            }
        }
        Ok(())
    }

    #[test]
    fn rejects_certificates_outside_the_verified_summary() -> Result<()> {
        let (output, identity, trust) = synthetic_provenance_fixture()?;
        let certificate = output[0]["verificationResult"]["signature"]["certificate"].clone();
        for invalid in [Value::Null, json!([]), json!("invalid")] {
            let mut changed = output.clone();
            changed[0]["attestation"] = json!({
                "signature": { "certificate": certificate }
            });
            changed[0]["verificationResult"]["signature"]["certificate"] = invalid;
            assert!(exact_verified_statement(&changed).is_err());
        }

        let mut nested = output;
        nested[0]["verificationResult"]["signature"]["certificate"] = json!({
            "extensions": certificate
        });
        assert!(validate_certificate(&nested[0]["verificationResult"], &identity, &trust).is_err());
        Ok(())
    }

    #[test]
    fn subject_admission_rejects_duplicate_or_extra_entries() {
        let expected = BTreeMap::from([("artifact.zip".to_owned(), "a".repeat(64))]);
        let duplicate = json!([
            {"name": "artifact.zip", "digest": {"sha256": "a".repeat(64)}},
            {"name": "artifact.zip", "digest": {"sha256": "a".repeat(64)}}
        ]);
        assert!(validate_subjects(&duplicate, &expected).is_err());
        let extra_algorithm = json!([
            {"name": "artifact.zip", "digest": {"sha256": "a".repeat(64), "sha1": "b".repeat(40)}}
        ]);
        assert!(validate_subjects(&extra_algorithm, &expected).is_err());
        let mismatched = BTreeMap::from([("other.zip".to_owned(), "a".repeat(64))]);
        assert!(validate_subjects(&duplicate, &mismatched).is_err());
    }
}
