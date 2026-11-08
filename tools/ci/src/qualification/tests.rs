use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::num::NonZeroUsize;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use tempfile::TempDir;
use zip::ZipWriter;
use zip::write::SimpleFileOptions;

use super::{
    Architecture, ExecutionContext, NativeOptions, Product, QualificationMode, QualifyArgs,
    RuntimeInputValidationReport, RuntimePairOrigin, inspect_runtime_pair,
};
use crate::model::Check;
use crate::process::{CommandOutput, CommandSpec, Runner};
use crate::tooling::load_toolset;

fn rejected<T>(result: Result<T>, reason: &str) -> Result<anyhow::Error> {
    match result {
        Ok(_) => anyhow::bail!("expected rejection: {reason}"),
        Err(error) => Ok(error),
    }
}

struct Fixture {
    _directory: TempDir,
    root: PathBuf,
    manifest: PathBuf,
    toolset: PathBuf,
    output: PathBuf,
    x64_sha256: String,
    x86_sha256: String,
    core: Vec<u8>,
    effects: Vec<u8>,
}

impl Fixture {
    fn new() -> Result<Self> {
        Self::with_build_info("Off", "Off", "0.1.0", "0.1.0", Architecture::X86)
    }

    fn with_build_info(
        x64_pgo: &str,
        x86_pgo: &str,
        x64_version: &str,
        x86_version: &str,
        x86_machine: Architecture,
    ) -> Result<Self> {
        let directory = tempfile::tempdir()?;
        let root = directory.path().join("source-data");
        fs::create_dir_all(root.join("third_party/inputnext/include"))?;
        let core = b"source-tag Core SDK header\n".to_vec();
        let effects = b"source-tag Effects SDK header\n".to_vec();
        fs::write(
            root.join("third_party/inputnext/include/input_next_core.h"),
            &core,
        )?;
        fs::write(
            root.join("third_party/inputnext/include/input_next_effects.h"),
            &effects,
        )?;
        let (x64_path, x64_sha256) = make_runtime_archive(
            &root,
            Architecture::X64,
            Architecture::X64,
            x64_version,
            x64_pgo,
            &core,
            &effects,
        )?;
        let (x86_path, x86_sha256) = make_runtime_archive(
            &root,
            Architecture::X86,
            x86_machine,
            x86_version,
            x86_pgo,
            &core,
            &effects,
        )?;
        let manifest = root.join("runtime-pair.json");
        write_local_manifest(
            &manifest,
            &core,
            &effects,
            &x64_path,
            &x64_sha256,
            &x86_path,
            &x86_sha256,
        )?;
        let toolset = root.join("toolset.json");
        write_toolset(&toolset, None)?;
        let output = directory.path().join("output");
        fs::create_dir_all(&output)?;
        Ok(Self {
            _directory: directory,
            root,
            manifest,
            toolset,
            output,
            x64_sha256,
            x86_sha256,
            core,
            effects,
        })
    }

    fn context(&self) -> Result<ExecutionContext> {
        let toolset = load_toolset(&self.toolset)?;
        Ok(ExecutionContext {
            repository: dunce::canonicalize(&self.root)?,
            output: dunce::canonicalize(&self.output)?,
            jobs: NonZeroUsize::new(1).context("one is nonzero")?,
            toolset,
            compiler_report: None,
            source: crate::model::SourceIdentity {
                commit: None,
                modified: true,
                snapshot_sha256: None,
            },
        })
    }

    fn install_gh(&mut self) -> Result<()> {
        let gh = self.root.join("fake-gh.exe");
        fs::write(&gh, b"fake gh executable")?;
        write_toolset(&self.toolset, Some(&gh))
    }
}

#[derive(Default)]
struct NoCommands {
    calls: usize,
    response: Option<Vec<u8>>,
}

impl Runner for NoCommands {
    fn run(&mut self, _spec: &CommandSpec) -> Result<CommandOutput> {
        self.calls += 1;
        let stdout = self
            .response
            .take()
            .context("unexpected external command during static qualification")?;
        Ok(CommandOutput {
            code: Some(0),
            stdout,
            stderr: Vec::new(),
        })
    }
}

#[test]
fn validate_input_stays_static_and_reports_input_only() -> Result<()> {
    let fixture = Fixture::new()?;
    let mut runner = NoCommands::default();
    let args = QualifyArgs {
        root: fixture.root.clone(),
        toolset: fixture.toolset.clone(),
        product: Product::Foundation,
        architecture: Architecture::X64,
        manifest: fixture.manifest.clone(),
        output: fixture.output.clone(),
        report: fixture.output.join("qualification-report.json"),
        mode: QualificationMode::Development,
        validate_input: true,
        jobs: None,
    };

    args.run(&mut runner)?;

    assert_eq!(
        runner.calls, 0,
        "static input validation must not run Git or native tools"
    );
    let report: RuntimeInputValidationReport =
        crate::json::parse(&fs::read(fixture.output.join("qualification-report.json"))?)?;
    assert_eq!(report.status, "input-validated");
    assert_eq!(report.runtime_pair.origin, RuntimePairOrigin::Local);
    assert_ne!(
        report.runtime_pair.x64.sha256,
        report.runtime_pair.x86.sha256
    );
    let raw: Value =
        serde_json::from_slice(&fs::read(fixture.output.join("qualification-report.json"))?)?;
    assert!(
        !raw.as_object()
            .context("report is not an object")?
            .contains_key("releaseEligible")
    );
    Ok(())
}

#[test]
fn rejects_bad_archive_hash_and_source_header_mismatch_before_extraction() -> Result<()> {
    let fixture = Fixture::new()?;
    let mut manifest: Value = serde_json::from_slice(&fs::read(&fixture.manifest)?)?;
    manifest["archives"]["x64"]["sha256"] = Value::String("0".repeat(64));
    fs::write(&fixture.manifest, serde_json::to_vec(&manifest)?)?;
    let context = fixture.context()?;
    let mut runner = NoCommands::default();
    assert!(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        )
        .is_err()
    );
    assert!(!fixture.output.join("runtimes").exists());

    let fixture = Fixture::new()?;
    fs::write(
        fixture
            .root
            .join("third_party/inputnext/include/input_next_core.h"),
        b"changed source tag header",
    )?;
    let context = fixture.context()?;
    assert!(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        )
        .is_err()
    );
    assert!(!fixture.output.join("runtimes").exists());
    assert_eq!(runner.calls, 0);
    Ok(())
}

#[test]
fn rejects_runtime_sdk_headers_that_do_not_match_the_source_tag() -> Result<()> {
    let fixture = Fixture::new()?;
    let (archive_path, archive_sha256) = make_runtime_archive(
        &fixture.root,
        Architecture::X64,
        Architecture::X64,
        "0.1.0",
        "Off",
        b"different packaged Core SDK header",
        &fixture.effects,
    )?;
    let mut manifest: Value = serde_json::from_slice(&fs::read(&fixture.manifest)?)?;
    manifest["archives"]["x64"]["path"] = Value::String(
        archive_path
            .file_name()
            .context("archive has no file name")?
            .to_string_lossy()
            .into_owned(),
    );
    manifest["archives"]["x64"]["sha256"] = Value::String(archive_sha256);
    fs::write(&fixture.manifest, serde_json::to_vec(&manifest)?)?;
    let context = fixture.context()?;
    let mut runner = NoCommands::default();
    let error = rejected(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        ),
        "runtime SDK bytes must equal the source-tag SDK bytes",
    )?;
    assert!(format!("{error:#}").contains("runtime Core SDK header"));
    assert_eq!(runner.calls, 0);
    Ok(())
}

#[test]
fn rejects_duplicate_json_and_local_release_origin() -> Result<()> {
    let fixture = Fixture::new()?;
    fs::write(
        &fixture.manifest,
        br#"{"schemaVersion":1,"schemaVersion":1,"kind":"local"}"#,
    )?;
    let context = fixture.context()?;
    let mut runner = NoCommands::default();
    let error = rejected(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        ),
        "duplicate JSON key must be rejected",
    )?;
    assert!(format!("{error:#}").contains("duplicate JSON object key"));

    let fixture = Fixture::new()?;
    let context = fixture.context()?;
    let error = rejected(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Release,
            &mut runner,
        ),
        "local runtime must not qualify a release",
    )?;
    assert!(format!("{error:#}").contains("published runtime pair"));
    assert_eq!(runner.calls, 0);
    Ok(())
}

#[test]
fn rejects_wrong_runtime_version_and_architecture() -> Result<()> {
    let fixture = Fixture::with_build_info("Off", "Off", "0.2.0", "0.1.0", Architecture::X86)?;
    let context = fixture.context()?;
    let mut runner = NoCommands::default();
    let error = rejected(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        ),
        "mismatched build version must be rejected",
    )?;
    assert!(format!("{error:#}").contains("version"));

    let fixture = Fixture::with_build_info("Off", "Off", "0.1.0", "0.1.0", Architecture::X64)?;
    let context = fixture.context()?;
    let error = rejected(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        ),
        "x64 PE bytes in the x86 archive must be rejected",
    )?;
    assert!(format!("{error:#}").contains("machine"));
    assert_eq!(runner.calls, 0);
    Ok(())
}

#[test]
fn accepts_off_or_optimize_pgo_and_rejects_instrumentation() -> Result<()> {
    let fixture = Fixture::with_build_info("Optimize", "Off", "0.1.0", "0.1.0", Architecture::X86)?;
    let context = fixture.context()?;
    let mut runner = NoCommands::default();
    let pair = inspect_runtime_pair(
        &fixture.manifest,
        &fixture.root,
        &context,
        QualificationMode::Development,
        &mut runner,
    )?;
    assert_eq!(pair.version, "0.1.0");

    let fixture =
        Fixture::with_build_info("Instrument", "Off", "0.1.0", "0.1.0", Architecture::X86)?;
    let context = fixture.context()?;
    assert!(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        )
        .is_err()
    );
    assert_eq!(runner.calls, 0);
    Ok(())
}

#[test]
fn published_release_identity_is_rechecked_before_any_download() -> Result<()> {
    let mut fixture = Fixture::new()?;
    fixture.install_gh()?;
    let manifest = json!({
        "schemaVersion": 1,
        "kind": "published",
        "version": "0.1.0",
        "headers": {
            "coreSha256": hash_bytes(&fixture.core),
            "effectsSha256": hash_bytes(&fixture.effects)
        },
        "repository": "reviewed/runtime",
        "releaseId": 7,
        "tag": "v0.1.0",
        "assets": {
            "x64": {"id": 11, "name": "x64.zip", "sha256": fixture.x64_sha256},
            "x86": {"id": 12, "name": "x86.zip", "sha256": fixture.x86_sha256}
        }
    });
    fs::write(&fixture.manifest, serde_json::to_vec(&manifest)?)?;
    let context = fixture.context()?;
    let mut runner = NoCommands {
        calls: 0,
        response: Some(
            json!({
                "id": 7,
                "tag_name": "v0.1.0",
                "url": "https://api.github.com/repos/attacker/runtime/releases/7",
                "draft": false,
                "prerelease": false,
                "assets": []
            })
            .to_string()
            .into_bytes(),
        ),
    };
    let error = rejected(
        inspect_runtime_pair(
            &fixture.manifest,
            &fixture.root,
            &context,
            QualificationMode::Development,
            &mut runner,
        ),
        "another repository's API identity must be rejected",
    )?;
    assert!(format!("{error:#}").contains("API identity"));
    assert_eq!(runner.calls, 1);
    assert!(!fixture.output.join("runtimes").exists());
    Ok(())
}

#[test]
fn published_runtime_pair_accepts_symbols_sboms_and_checksums() -> Result<()> {
    let fixture = published_fixture()?;
    let mut runner = release_runner(authentic_release_assets(&fixture));
    let pair = inspect_published_fixture(&fixture, &mut runner)?;

    assert_eq!(pair.origin, RuntimePairOrigin::Published);
    assert_eq!(pair.x64.asset_name.as_deref(), Some(X64_RUNTIME_ASSET));
    assert_eq!(pair.x86.asset_name.as_deref(), Some(X86_RUNTIME_ASSET));
    assert_eq!(
        runner.calls, 1,
        "only the release identity API call should run"
    );
    Ok(())
}

#[test]
fn published_runtime_assets_must_be_unique_present_and_exact() -> Result<()> {
    let fixture = published_fixture()?;

    let mut duplicate_id = authentic_release_assets(&fixture);
    duplicate_id.push(release_asset(
        11,
        "another-metadata.json",
        &hash_bytes(b"duplicate id"),
    ));
    rejects_release_assets(&fixture, duplicate_id, "repeats an asset id")?;

    let mut missing_x86 = authentic_release_assets(&fixture);
    missing_x86.remove(1);
    rejects_release_assets(&fixture, missing_x86, "has no asset id 12")?;

    let mut wrong_name = authentic_release_assets(&fixture);
    wrong_name[0]["name"] = json!("InputNext-0.1.0-windows-x64-other.zip");
    rejects_release_assets(&fixture, wrong_name, "id has another name")?;

    let mut duplicate_name = authentic_release_assets(&fixture);
    duplicate_name.push(release_asset(
        18,
        X64_RUNTIME_ASSET,
        &hash_bytes(b"ambiguous runtime name"),
    ));
    rejects_release_assets(&fixture, duplicate_name, "name is missing or ambiguous")?;
    Ok(())
}

#[test]
fn published_runtime_asset_digest_must_match_the_manifest() -> Result<()> {
    let fixture = published_fixture()?;
    let mut assets = authentic_release_assets(&fixture);
    assets[0]["digest"] = json!(format!("sha256:{}", "0".repeat(64)));

    rejects_release_assets(
        &fixture,
        assets,
        "digest differs from the trusted manifest hash",
    )
}

#[test]
fn missing_required_cold_test_is_rejected_before_native_execution() -> Result<()> {
    let fixture = Fixture::new()?;
    let context = fixture.context()?;
    let mut runner = NoCommands::default();
    let pair = inspect_runtime_pair(
        &fixture.manifest,
        &fixture.root,
        &context,
        QualificationMode::Development,
        &mut runner,
    )?;
    let options = NativeOptions {
        runtime_directory: Some(pair.x64.bin_directory.clone()),
        ..NativeOptions::default()
    };
    let plan = crate::plan::create_plan(
        &context.repository,
        &context.output,
        Product::Foundation,
        Architecture::X64,
        Check::Tests,
        &options,
    )?;
    let mut required = plan.required_tests.clone();
    required.pop().context("fixture has no required tests")?;

    let error = rejected(
        super::qualify_runtime_pair(&pair, &plan, &context, &required, &options, &mut runner),
        "a missing cold case must be rejected",
    )?;
    assert!(format!("{error:#}").contains("exact SDK-free test plan list"));
    assert_eq!(runner.calls, 0);

    Ok(())
}

fn write_local_manifest(
    path: &Path,
    core: &[u8],
    effects: &[u8],
    x64_path: &Path,
    x64_sha256: &str,
    x86_path: &Path,
    x86_sha256: &str,
) -> Result<()> {
    let manifest_root = path.parent().context("manifest has no parent")?;
    let manifest = json!({
        "schemaVersion": 1,
        "kind": "local",
        "version": "0.1.0",
        "headers": {
            "coreSha256": hash_bytes(core),
            "effectsSha256": hash_bytes(effects)
        },
        "archives": {
            "x64": {
                "path": x64_path.strip_prefix(manifest_root)?.to_string_lossy(),
                "sha256": x64_sha256
            },
            "x86": {
                "path": x86_path.strip_prefix(manifest_root)?.to_string_lossy(),
                "sha256": x86_sha256
            }
        }
    });
    fs::write(path, serde_json::to_vec_pretty(&manifest)?)?;
    Ok(())
}

fn make_runtime_archive(
    root: &Path,
    architecture: Architecture,
    pe_architecture: Architecture,
    version: &str,
    pgo: &str,
    core: &[u8],
    effects: &[u8],
) -> Result<(PathBuf, String)> {
    let mut files = BTreeMap::new();
    for name in [
        "GameInputRedist.dll",
        "GameInputRawInputProxy.exe",
        "inputnext_runtime.dll",
        "xinput1_3.dll",
        "xinput1_4.dll",
        "xinput9_1_0.dll",
    ] {
        files.insert(format!("bin/{name}"), pe_fixture(pe_architecture));
    }
    files.insert("sdk/input_next_core.h".to_owned(), core.to_vec());
    files.insert("sdk/input_next_effects.h".to_owned(), effects.to_vec());
    files.insert(
        "build-info.json".to_owned(),
        serde_json::to_vec(&json!({
            "name": "InputNext",
            "version": version,
            "architecture": architecture.as_str(),
            "compiler": "MSVC 19.51.36260.0",
            "configuration": "Release",
            "optimization": "LTCG",
            "pgo": pgo,
            "gameinput": "4.0.306",
            "tomlplusplus": "3.4.0",
            "source_revision": "1".repeat(40),
            "source_modified": false
        }))?,
    );
    let hashes = files
        .iter()
        .map(|(path, bytes)| (path.clone(), hash_bytes(bytes)))
        .collect::<BTreeMap<_, _>>();
    let content_manifest = serde_json::to_vec(&json!({ "schema": 1, "files": hashes }))?;
    let archive_path = root.join(format!("runtime-{}.zip", architecture.as_str()));
    let mut writer = ZipWriter::new(fs::File::create(&archive_path)?);
    let options = SimpleFileOptions::default();
    for (path, bytes) in files {
        writer.start_file(path, options)?;
        writer.write_all(&bytes)?;
    }
    writer.start_file("manifest.json", options)?;
    writer.write_all(&content_manifest)?;
    writer.finish()?;
    let sha256 = hash_bytes(&fs::read(&archive_path)?);
    Ok((archive_path, sha256))
}

fn pe_fixture(architecture: Architecture) -> Vec<u8> {
    let mut bytes = vec![0_u8; 0x100];
    bytes[0..2].copy_from_slice(b"MZ");
    bytes[0x3c..0x40].copy_from_slice(&0x40_u32.to_le_bytes());
    bytes[0x40..0x44].copy_from_slice(b"PE\0\0");
    let (machine, magic) = match architecture {
        Architecture::X64 => (0x8664_u16, 0x020b_u16),
        Architecture::X86 => (0x014c_u16, 0x010b_u16),
    };
    bytes[0x44..0x46].copy_from_slice(&machine.to_le_bytes());
    bytes[0x54..0x56].copy_from_slice(&0x20_u16.to_le_bytes());
    bytes[0x58..0x5a].copy_from_slice(&magic.to_le_bytes());
    bytes
}

fn hash_bytes(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut output = String::with_capacity(64);
    for byte in Sha256::digest(bytes) {
        output.push(char::from(HEX[usize::from(byte >> 4)]));
        output.push(char::from(HEX[usize::from(byte & 0x0f)]));
    }
    output
}

const X64_RUNTIME_ASSET: &str = "InputNext-0.1.0-windows-x64.zip";
const X86_RUNTIME_ASSET: &str = "InputNext-0.1.0-windows-x86.zip";

fn published_fixture() -> Result<Fixture> {
    let mut fixture = Fixture::new()?;
    fixture.install_gh()?;
    let manifest = json!({
        "schemaVersion": 1,
        "kind": "published",
        "version": "0.1.0",
        "headers": {
            "coreSha256": hash_bytes(&fixture.core),
            "effectsSha256": hash_bytes(&fixture.effects)
        },
        "repository": "reviewed/runtime",
        "releaseId": 7,
        "tag": "v0.1.0",
        "assets": {
            "x64": {"id": 11, "name": X64_RUNTIME_ASSET, "sha256": fixture.x64_sha256},
            "x86": {"id": 12, "name": X86_RUNTIME_ASSET, "sha256": fixture.x86_sha256}
        }
    });
    fs::write(&fixture.manifest, serde_json::to_vec(&manifest)?)?;
    let cache = fixture.output.join("cache/runtime");
    fs::create_dir_all(&cache)?;
    for (architecture, sha256) in [("x64", &fixture.x64_sha256), ("x86", &fixture.x86_sha256)] {
        fs::copy(
            fixture.root.join(format!("runtime-{architecture}.zip")),
            cache.join(format!("{sha256}.archive")),
        )?;
    }
    Ok(fixture)
}

fn release_asset(id: u64, name: &str, sha256: &str) -> Value {
    json!({
        "id": id,
        "name": name,
        "digest": format!("sha256:{sha256}"),
        "browser_download_url": format!(
            "https://github.com/reviewed/runtime/releases/download/v0.1.0/{name}"
        )
    })
}

fn authentic_release_assets(fixture: &Fixture) -> Vec<Value> {
    vec![
        release_asset(11, X64_RUNTIME_ASSET, &fixture.x64_sha256),
        release_asset(12, X86_RUNTIME_ASSET, &fixture.x86_sha256),
        release_asset(
            13,
            "InputNext-0.1.0-windows-x64-symbols.zip",
            &hash_bytes(b"x64 symbols"),
        ),
        release_asset(
            14,
            "InputNext-0.1.0-windows-x86-symbols.zip",
            &hash_bytes(b"x86 symbols"),
        ),
        release_asset(
            15,
            "InputNext-0.1.0-windows-x64.spdx.json",
            &hash_bytes(b"x64 SBOM"),
        ),
        release_asset(
            16,
            "InputNext-0.1.0-windows-x86.spdx.json",
            &hash_bytes(b"x86 SBOM"),
        ),
        release_asset(17, "SHA256SUMS", &hash_bytes(b"release checksums")),
    ]
}

fn release_response(assets: Vec<Value>) -> Vec<u8> {
    json!({
        "id": 7,
        "tag_name": "v0.1.0",
        "url": "https://api.github.com/repos/reviewed/runtime/releases/7",
        "draft": false,
        "prerelease": false,
        "assets": Value::Array(assets)
    })
    .to_string()
    .into_bytes()
}

fn release_runner(assets: Vec<Value>) -> NoCommands {
    NoCommands {
        calls: 0,
        response: Some(release_response(assets)),
    }
}

fn inspect_published_fixture(
    fixture: &Fixture,
    runner: &mut dyn Runner,
) -> Result<super::VerifiedRuntimePair> {
    inspect_runtime_pair(
        &fixture.manifest,
        &fixture.root,
        &fixture.context()?,
        QualificationMode::Development,
        runner,
    )
}

fn rejects_release_assets(
    fixture: &Fixture,
    assets: Vec<Value>,
    expected_error: &str,
) -> Result<()> {
    let mut runner = release_runner(assets);
    let error = rejected(
        inspect_published_fixture(fixture, &mut runner),
        expected_error,
    )?;
    assert!(format!("{error:#}").contains(expected_error));
    assert_eq!(runner.calls, 1);
    Ok(())
}

fn write_toolset(path: &Path, gh: Option<&Path>) -> Result<()> {
    let tools = gh
        .map(|path| {
            BTreeMap::from([(
                "gh",
                json!({
                    "version": "2.0.0",
                    "path": path,
                    "kind": "executable",
                    "archive": null,
                    "sha256": "a".repeat(64)
                }),
            )])
        })
        .unwrap_or_default();
    let report = json!({
        "schema_version": 1,
        "profile": "test",
        "platform": "linux-x64",
        "lock_sha256": "b".repeat(64),
        "tools": tools,
        "environment": {}
    });
    fs::write(path, serde_json::to_vec(&report)?)?;
    Ok(())
}
