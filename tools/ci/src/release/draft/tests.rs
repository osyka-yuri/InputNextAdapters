use super::{
    DraftIdentity, create_draft, encode_path_segment, require_tagged_release_absent,
    response_status, upload_verified_assets,
};
use crate::model::Product;
use crate::process::{CommandOutput, CommandSpec, Runner};
use crate::release::verify::{ReleaseReport, VerifiedAsset};
use anyhow::{Context, Result};
use serde_json::{Value, json};
use std::collections::{BTreeMap, VecDeque};
use std::path::Path;

use super::super::identity::ReleaseIdentity;

#[derive(Debug, Default)]
struct FakeRunner {
    outputs: VecDeque<CommandOutput>,
    calls: Vec<Vec<String>>,
    request: Option<Value>,
}

impl Runner for FakeRunner {
    fn run(&mut self, command: &CommandSpec) -> Result<CommandOutput> {
        let args = command
            .args
            .iter()
            .map(|argument| argument.to_string_lossy().into_owned())
            .collect::<Vec<_>>();
        if let Some(index) = args.iter().position(|argument| argument == "--input") {
            let path = args
                .get(index + 1)
                .context("draft request path is missing")?;
            let bytes = std::fs::read(path)?;
            self.request = Some(crate::json::parse_slice(&bytes)?);
        }
        self.calls.push(args);
        self.outputs
            .pop_front()
            .context("fake runner has no configured response")
    }
}

#[test]
fn release_tag_path_segment_encodes_slashes_and_percent() {
    assert_eq!(
        encode_path_segment("foundation/v1.2.3"),
        "foundation%2Fv1.2.3"
    );
    assert_eq!(encode_path_segment("foundation/%2f"), "foundation%2F%252f");
}

#[test]
fn parses_final_http_status_after_redirect_headers() -> Result<()> {
    assert_eq!(response_status(b"HTTP/1.1 302 Found\r\nLocation: /x\r\n\r\nHTTP/2 404\r\ncontent-type: application/json\r\n\r\n{}")?, 404);
    assert!(response_status(b"release not found").is_err());
    Ok(())
}

#[test]
fn draft_request_is_pinned_to_the_tag_and_never_publishes() -> Result<()> {
    let root = tempfile::tempdir()?;
    let identity = identity();
    let mut runner = FakeRunner {
        outputs: VecDeque::from([CommandOutput {
            code: Some(0),
            stdout: serde_json::to_vec(&json!({
                "id": 42,
                "tag_name": identity.tag,
                "draft": true,
                "prerelease": false,
                "html_url": "https://example.invalid/release"
            }))?,
            stderr: Vec::new(),
        }]),
        ..FakeRunner::default()
    };
    let draft = create_draft(
        &mut runner,
        Path::new("gh"),
        &BTreeMap::new(),
        root.path(),
        &identity,
        true,
    )?;
    assert_eq!(draft.id, 42);
    let request = runner.request.context("draft request was not captured")?;
    assert_eq!(request["tag_name"], identity.tag);
    assert_eq!(request["target_commitish"], identity.source_commit);
    assert_eq!(request["draft"], true);
    assert_eq!(request["prerelease"], false);
    assert_eq!(request["make_latest"], "false");
    assert!(runner.calls[0].contains(&"POST".to_owned()));
    assert!(
        runner.calls[0]
            .iter()
            .all(|argument| argument != "--clobber")
    );
    Ok(())
}

#[test]
fn existing_release_collision_stops_before_upload() -> Result<()> {
    let root = tempfile::tempdir()?;
    let identity = identity();
    let mut runner = FakeRunner {
        outputs: VecDeque::from([CommandOutput {
            code: Some(0),
            stdout: b"HTTP/2 200 OK\r\ncontent-type: application/json\r\n\r\n{}".to_vec(),
            stderr: Vec::new(),
        }]),
        ..FakeRunner::default()
    };
    assert!(
        require_tagged_release_absent(
            &mut runner,
            Path::new("gh"),
            &BTreeMap::new(),
            root.path(),
            &identity.repository,
            &identity.tag,
        )
        .is_err()
    );
    assert_eq!(runner.calls.len(), 1);
    assert!(runner.calls[0].contains(&"--include".to_owned()));
    Ok(())
}

#[test]
fn partial_upload_is_reported_without_clobber_or_retry() -> Result<()> {
    let root = tempfile::tempdir()?;
    let first_path = root
        .path()
        .join("InputNextAdapters-skyrim-se-1.2.3-windows-x64.zip");
    let second_path = root
        .path()
        .join("InputNextAdapters-skyrim-se-1.2.3-source.zip");
    std::fs::write(&first_path, b"verified package")?;
    std::fs::write(&second_path, b"verified source")?;
    let assets = vec![
        VerifiedAsset {
            name: first_path
                .file_name()
                .context("fixture asset has no filename")?
                .to_string_lossy()
                .into_owned(),
            sha256: "a".repeat(64),
            size_bytes: 16,
            artifact_id: 7,
        },
        VerifiedAsset {
            name: second_path
                .file_name()
                .context("fixture asset has no filename")?
                .to_string_lossy()
                .into_owned(),
            sha256: "b".repeat(64),
            size_bytes: 15,
            artifact_id: 7,
        },
    ];
    let draft = DraftIdentity {
        id: 42,
        html_url: None,
    };
    let mut report = report();
    report.draft_id = Some(draft.id);
    let report_path = root.path().join("release-report.json");
    let mut runner = FakeRunner {
        outputs: VecDeque::from([
            CommandOutput {
                code: Some(0),
                stdout: Vec::new(),
                stderr: Vec::new(),
            },
            CommandOutput {
                code: Some(1),
                stdout: Vec::new(),
                stderr: b"upload failed".to_vec(),
            },
        ]),
        ..FakeRunner::default()
    };
    let result = upload_verified_assets(
        &mut runner,
        Path::new("gh"),
        &BTreeMap::new(),
        root.path(),
        &identity(),
        &draft,
        &[first_path, second_path],
        &assets,
        &mut report,
        Some(&report_path),
    );
    assert!(result.is_err(), "the second fake upload must fail");
    let error = result.err().context("fake upload error was lost")?;
    assert!(format!("{error:#}").contains("draft release skyrim-se/v1.2.3 was created (ID 42)"));
    assert_eq!(runner.calls.len(), 2);
    assert!(
        runner
            .calls
            .iter()
            .all(|call| !call.iter().any(|arg| arg == "--clobber"))
    );
    assert_eq!(report.state, "partial-draft");
    assert_eq!(report.uploaded_assets, vec![assets[0].name.clone()]);
    let saved: Value = crate::json::parse_slice(&std::fs::read(report_path)?)?;
    assert_eq!(saved["state"], "partial-draft");
    assert_eq!(saved["draftId"], 42);
    assert_eq!(saved["uploadedAssets"][0], assets[0].name);
    Ok(())
}

#[test]
fn upload_verified_assets_admits_package_zip_and_sigstore_provenance_bundle() -> Result<()> {
    let root = tempfile::tempdir()?;
    let zip_path = root
        .path()
        .join("InputNextAdapters-skyrim-se-1.2.3-windows-x64.zip");
    let sigstore_path = root
        .path()
        .join("InputNextAdapters-skyrim-se-1.2.3-provenance.sigstore.json");
    std::fs::write(&zip_path, b"verified package")?;
    std::fs::write(&sigstore_path, b"verified provenance bundle")?;
    let assets = vec![
        VerifiedAsset {
            name: zip_path
                .file_name()
                .context("fixture asset has no filename")?
                .to_string_lossy()
                .into_owned(),
            sha256: "a".repeat(64),
            size_bytes: 16,
            artifact_id: 7,
        },
        VerifiedAsset {
            name: sigstore_path
                .file_name()
                .context("fixture asset has no filename")?
                .to_string_lossy()
                .into_owned(),
            sha256: "b".repeat(64),
            size_bytes: 26,
            artifact_id: 8,
        },
    ];
    let draft = DraftIdentity {
        id: 42,
        html_url: None,
    };
    let mut report = report();
    report.draft_id = Some(draft.id);
    let mut runner = FakeRunner {
        outputs: VecDeque::from([
            CommandOutput {
                code: Some(0),
                stdout: Vec::new(),
                stderr: Vec::new(),
            },
            CommandOutput {
                code: Some(0),
                stdout: Vec::new(),
                stderr: Vec::new(),
            },
        ]),
        ..FakeRunner::default()
    };
    let asset_paths = [zip_path, sigstore_path];
    upload_verified_assets(
        &mut runner,
        Path::new("gh"),
        &BTreeMap::new(),
        root.path(),
        &identity(),
        &draft,
        &asset_paths,
        &assets,
        &mut report,
        None,
    )?;
    assert_eq!(runner.calls.len(), 2);
    for (call, path) in runner.calls.iter().zip(&asset_paths) {
        assert_eq!(call[0], "release");
        assert_eq!(call[1], "upload");
        assert_eq!(call[2], identity().tag);
        assert_eq!(call[3], path.to_string_lossy());
        assert_eq!(call[4], "--repo");
        assert_eq!(call[5], identity().repository);
        assert_eq!(call.len(), 6);
    }
    assert_eq!(
        report.uploaded_assets,
        vec![assets[0].name.clone(), assets[1].name.clone()]
    );
    Ok(())
}

#[test]
fn upload_verified_assets_rejects_unrecognized_asset_extension() -> Result<()> {
    let root = tempfile::tempdir()?;
    for name in [
        "InputNextAdapters-skyrim-se-1.2.3-metadata.json",
        "InputNextAdapters-skyrim-se-1.2.3-provenance.sigstore.json.bak",
    ] {
        let bad_path = root.path().join(name);
        std::fs::write(&bad_path, b"unexpected payload")?;
        let assets = vec![VerifiedAsset {
            name: name.to_owned(),
            sha256: "c".repeat(64),
            size_bytes: 18,
            artifact_id: 9,
        }];
        let draft = DraftIdentity {
            id: 42,
            html_url: None,
        };
        let mut report = report();
        report.draft_id = Some(draft.id);
        let mut runner = FakeRunner::default();
        let result = upload_verified_assets(
            &mut runner,
            Path::new("gh"),
            &BTreeMap::new(),
            root.path(),
            &identity(),
            &draft,
            &[bad_path],
            &assets,
            &mut report,
            None,
        );
        assert!(result.is_err(), "asset '{name}' must be rejected");
        let error = result.err().context("unexpected error absence")?;
        assert!(
            format!("{error:#}")
                .contains("not an expected package ZIP or provenance attestation bundle")
        );
        assert_eq!(runner.calls.len(), 0);
    }
    Ok(())
}

fn identity() -> ReleaseIdentity {
    ReleaseIdentity {
        schema_version: 1,
        repository: "example/adapters".to_owned(),
        product: Product::SkyrimSe,
        version: "1.2.3".to_owned(),
        tag: "skyrim-se/v1.2.3".to_owned(),
        source_commit: "a".repeat(40),
        workflow_commit: "b".repeat(40),
        run_id: "5".to_owned(),
        run_attempt: "2".to_owned(),
        notes: "release notes".to_owned(),
    }
}

fn report() -> ReleaseReport {
    ReleaseReport {
        schema_version: 1,
        state: "draft-created".to_owned(),
        repository: "example/adapters".to_owned(),
        product: Product::SkyrimSe,
        version: "1.2.3".to_owned(),
        tag: "skyrim-se/v1.2.3".to_owned(),
        source_commit: "a".repeat(40),
        workflow_commit: "b".repeat(40),
        run_id: "5".to_owned(),
        run_attempt: "2".to_owned(),
        runtime_version: "1.2.3".to_owned(),
        runtime_repository: Some("example/runtime".to_owned()),
        runtime_release_id: Some(1),
        runtime_tag: Some("runtime/v1.2.3".to_owned()),
        runtime_core_sha256: "c".repeat(64),
        runtime_effects_sha256: "d".repeat(64),
        runtime_x64_sha256: "e".repeat(64),
        runtime_x86_sha256: "f".repeat(64),
        assets: Vec::new(),
        draft_id: None,
        draft_url: None,
        uploaded_assets: Vec::new(),
        failure: None,
    }
}
