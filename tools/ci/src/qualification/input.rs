use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use serde::Deserialize;

use crate::model::{Architecture, ExecutionContext};
use crate::process::{CommandSpec, Runner};
use crate::tooling::{Toolset, download_verified_archive, validate_sha256};
use sha2::{Digest, Sha256};

use super::{QualificationMode, RuntimePairOrigin, VerifiedRuntimePair};

const MAX_MANIFEST_BYTES: u64 = 4 * 1024 * 1024;
const GH_TIMEOUT: Duration = Duration::from_secs(60);
const GH_OUTPUT_LIMIT: usize = 16 * 1024 * 1024;

#[derive(Clone, Debug)]
pub(super) struct RuntimePairInput {
    pub origin: RuntimePairOrigin,
    pub repository: Option<String>,
    pub release_id: Option<u64>,
    pub tag: Option<String>,
    pub version: String,
    pub core_sha256: String,
    pub effects_sha256: String,
    pub manifest_sha256: String,
    pub archives: [RuntimeArchiveInput; 2],
}

#[derive(Clone, Debug)]
pub(super) struct RuntimeArchiveInput {
    pub architecture: Architecture,
    pub path: PathBuf,
    pub sha256: String,
    pub asset_id: Option<u64>,
    pub asset_name: Option<String>,
}

struct ResolvedPairInput {
    origin: RuntimePairOrigin,
    repository: Option<String>,
    release_id: Option<u64>,
    tag: Option<String>,
    version: String,
    headers: RuntimeHeaders,
    archives: [RuntimeArchiveInput; 2],
}

pub(super) fn inspect_runtime_pair(
    manifest_path: &Path,
    data_root: &Path,
    context: &ExecutionContext,
    mode: QualificationMode,
    runner: &mut dyn Runner,
) -> Result<VerifiedRuntimePair> {
    ensure!(
        context.repository.is_absolute() && context.output.is_absolute(),
        "runtime qualification context paths must be absolute"
    );
    let repository = dunce::canonicalize(&context.repository)
        .context("could not resolve qualification repository")?;
    let output =
        dunce::canonicalize(&context.output).context("could not resolve qualification output")?;
    ensure!(
        repository == context.repository && repository.is_dir(),
        "qualification repository must be an absolute canonical directory"
    );
    ensure!(
        output == context.output && output.is_dir(),
        "qualification output must be an absolute canonical directory"
    );
    let data_root = dunce::canonicalize(data_root).with_context(|| {
        format!(
            "could not resolve runtime data root {}",
            data_root.display()
        )
    })?;
    ensure!(data_root.is_dir(), "runtime data root is not a directory");
    let input = read_runtime_input(manifest_path, context, mode, runner)?;
    super::payload::finish_runtime_pair(input, &data_root, context)
}

pub(super) fn verify_published_pair(
    pair: &VerifiedRuntimePair,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<()> {
    if pair.origin == RuntimePairOrigin::Local {
        return Ok(());
    }
    let repository = pair
        .repository
        .as_deref()
        .context("published runtime pair has no repository identity")?;
    let release_id = pair
        .release_id
        .context("published runtime pair has no release id")?;
    let tag = pair
        .tag
        .as_deref()
        .context("published runtime pair has no release tag")?;
    validate_published_identity(repository, tag, &pair.version)?;
    let assets = RuntimePublishedAssets {
        x64: RuntimePublishedAsset {
            id: pair.x64.asset_id.context("published x64 asset has no id")?,
            name: pair
                .x64
                .asset_name
                .clone()
                .context("published x64 asset has no name")?,
            sha256: pair.x64.sha256.clone(),
        },
        x86: RuntimePublishedAsset {
            id: pair.x86.asset_id.context("published x86 asset has no id")?,
            name: pair
                .x86
                .asset_name
                .clone()
                .context("published x86 asset has no name")?,
            sha256: pair.x86.sha256.clone(),
        },
    };
    inspect_published_release(repository, release_id, tag, &assets, context, runner)?;
    Ok(())
}

pub(super) fn validate_published_identity(
    repository: &str,
    tag: &str,
    version: &str,
) -> Result<()> {
    validate_repository(repository)?;
    validate_tag(tag)?;
    let version = stable_version(version)?;
    ensure!(
        tag == format!("v{version}") || tag.ends_with(&format!("/v{version}")),
        "published runtime tag does not match its version"
    );
    Ok(())
}

fn read_runtime_input(
    manifest_path: &Path,
    context: &ExecutionContext,
    mode: QualificationMode,
    runner: &mut dyn Runner,
) -> Result<RuntimePairInput> {
    let (manifest_path, manifest_bytes, manifest) = read_runtime_manifest(manifest_path)?;
    let manifest_sha256 = hash_bytes(&manifest_bytes);
    let resolved = match manifest.pair {
        RuntimeManifestKind::Local {
            version,
            headers,
            archives,
        } => resolve_local_pair(&version, headers, &archives, &manifest_path, mode)?,
        RuntimeManifestKind::Published {
            version,
            headers,
            repository,
            release_id,
            tag,
            assets,
        } => resolve_published_pair(
            &version, headers, repository, release_id, tag, assets, context, runner,
        )?,
    };
    let core_sha256 = normalized_sha256(&resolved.headers.core_sha256)?;
    let effects_sha256 = normalized_sha256(&resolved.headers.effects_sha256)?;
    for archive in &resolved.archives {
        validate_sha256(&archive.sha256)?;
    }
    Ok(RuntimePairInput {
        origin: resolved.origin,
        repository: resolved.repository,
        release_id: resolved.release_id,
        tag: resolved.tag,
        version: resolved.version,
        core_sha256,
        effects_sha256,
        manifest_sha256,
        archives: resolved.archives,
    })
}

fn read_runtime_manifest(manifest_path: &Path) -> Result<(PathBuf, Vec<u8>, RuntimeManifest)> {
    let metadata = fs::symlink_metadata(manifest_path).with_context(|| {
        format!(
            "could not inspect runtime manifest {}",
            manifest_path.display()
        )
    })?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= MAX_MANIFEST_BYTES,
        "runtime manifest must be a bounded regular file"
    );
    let manifest_path = dunce::canonicalize(manifest_path).with_context(|| {
        format!(
            "could not resolve runtime manifest {}",
            manifest_path.display()
        )
    })?;
    let manifest_bytes = fs::read(&manifest_path).context("could not read runtime manifest")?;
    let raw = crate::json::parse_slice(&manifest_bytes)
        .context("runtime manifest is not valid strict JSON")?;
    if raw.get("published").is_some_and(serde_json::Value::is_null) {
        anyhow::bail!("runtime manifest contains no accepted published pair");
    }
    validate_manifest_fields(&raw)?;
    let manifest: RuntimeManifest = crate::json::parse(&manifest_bytes)
        .context("runtime manifest has an unsupported or malformed schema")?;
    ensure!(
        manifest.schema_version == 1,
        "unsupported runtime manifest schema"
    );
    Ok((manifest_path, manifest_bytes, manifest))
}

fn resolve_local_pair(
    version: &str,
    headers: RuntimeHeaders,
    archives: &RuntimeInputArchives,
    manifest_path: &Path,
    mode: QualificationMode,
) -> Result<ResolvedPairInput> {
    ensure!(
        mode == QualificationMode::Development,
        "release qualification requires a published runtime pair"
    );
    let version = stable_version(version)?;
    let root = manifest_path
        .parent()
        .context("runtime manifest has no parent directory")?;
    Ok(ResolvedPairInput {
        origin: RuntimePairOrigin::Local,
        repository: None,
        release_id: None,
        tag: None,
        version,
        headers,
        archives: [
            local_archive(Architecture::X64, root, &archives.x64)?,
            local_archive(Architecture::X86, root, &archives.x86)?,
        ],
    })
}

#[allow(
    clippy::too_many_arguments,
    reason = "published identity fields remain explicit at the service verification boundary"
)]
fn resolve_published_pair(
    version: &str,
    headers: RuntimeHeaders,
    repository: String,
    release_id: u64,
    tag: String,
    assets: RuntimePublishedAssets,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<ResolvedPairInput> {
    validate_repository(&repository)?;
    ensure!(
        release_id > 0,
        "published runtime release id must be nonzero"
    );
    validate_tag(&tag)?;
    let version = stable_version(version)?;
    ensure!(
        tag == format!("v{version}") || tag.ends_with(&format!("/v{version}")),
        "published runtime tag does not match its version"
    );
    ensure!(
        assets.x64.id > 0 && assets.x86.id > 0 && assets.x64.id != assets.x86.id,
        "published x64 and x86 runtime assets must have distinct nonzero ids"
    );
    validate_asset_name(&assets.x64.name)?;
    validate_asset_name(&assets.x86.name)?;
    ensure!(
        assets.x64.name != assets.x86.name,
        "published x64 and x86 runtime assets must have distinct names"
    );
    let x64_sha256 = normalized_sha256(&assets.x64.sha256)?;
    let x86_sha256 = normalized_sha256(&assets.x86.sha256)?;
    let release =
        inspect_published_release(&repository, release_id, &tag, &assets, context, runner)?;
    let cache = context.output.join("cache/runtime");
    let x64 = download_published_archive(
        runner,
        &cache,
        &release,
        Architecture::X64,
        assets.x64,
        x64_sha256,
    )?;
    let x86 = download_published_archive(
        runner,
        &cache,
        &release,
        Architecture::X86,
        assets.x86,
        x86_sha256,
    )?;
    Ok(ResolvedPairInput {
        origin: RuntimePairOrigin::Published,
        repository: Some(repository),
        release_id: Some(release_id),
        tag: Some(tag),
        version,
        headers,
        archives: [x64, x86],
    })
}

fn download_published_archive(
    runner: &mut dyn Runner,
    cache: &Path,
    release: &PublishedRelease,
    architecture: Architecture,
    asset: RuntimePublishedAsset,
    sha256: String,
) -> Result<RuntimeArchiveInput> {
    let url = release
        .assets
        .get(&asset.id)
        .with_context(|| format!("published runtime asset {} is missing", asset.id))?;
    let path = download_verified_archive(runner, cache, url, &sha256, "zip")?;
    Ok(RuntimeArchiveInput {
        architecture,
        path,
        sha256,
        asset_id: Some(asset.id),
        asset_name: Some(asset.name),
    })
}

fn local_archive(
    architecture: Architecture,
    manifest_root: &Path,
    input: &RuntimeLocalArchive,
) -> Result<RuntimeArchiveInput> {
    let path = Path::new(&input.path);
    let path = if path.is_absolute() {
        path.to_path_buf()
    } else {
        manifest_root.join(path)
    };
    let metadata = fs::symlink_metadata(&path)
        .with_context(|| format!("could not inspect local runtime archive {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "local runtime archive must be a regular file"
    );
    Ok(RuntimeArchiveInput {
        architecture,
        path: dunce::canonicalize(path).context("could not resolve local runtime archive")?,
        sha256: normalized_sha256(&input.sha256)?,
        asset_id: None,
        asset_name: None,
    })
}

fn inspect_published_release(
    repository: &str,
    release_id: u64,
    tag: &str,
    assets: &RuntimePublishedAssets,
    context: &ExecutionContext,
    runner: &mut dyn Runner,
) -> Result<PublishedRelease> {
    let gh = context.toolset.executable("gh")?;
    let api_path = format!("repos/{repository}/releases/{release_id}");
    let output = runner.run(&CommandSpec {
        program: gh.to_path_buf(),
        args: vec!["api".into(), api_path.into()],
        cwd: context.repository.clone(),
        env: tool_environment(&context.toolset),
        timeout: GH_TIMEOUT,
        output_limit: GH_OUTPUT_LIMIT,
    })?;
    ensure!(
        output.code == Some(0),
        "GitHub release metadata lookup failed\n{}",
        output.diagnostics()
    );
    let response = crate::json::parse_slice(&output.stdout)
        .context("GitHub release metadata response is malformed")?;
    ensure!(
        response["id"].as_u64() == Some(release_id),
        "GitHub returned another release id"
    );
    ensure!(
        response["tag_name"].as_str() == Some(tag),
        "GitHub release tag does not match the trusted manifest"
    );
    let expected_api_url =
        format!("https://api.github.com/repos/{repository}/releases/{release_id}");
    ensure!(
        response["url"]
            .as_str()
            .is_some_and(|url| url.eq_ignore_ascii_case(&expected_api_url)),
        "GitHub release API identity does not match the trusted repository and release id"
    );
    ensure!(
        response["draft"].as_bool() == Some(false)
            && response["prerelease"].as_bool() == Some(false),
        "runtime qualification rejects draft or prerelease releases"
    );
    inspect_selected_assets(&response, repository, tag, assets)
}

fn inspect_selected_assets(
    response: &serde_json::Value,
    repository: &str,
    tag: &str,
    assets: &RuntimePublishedAssets,
) -> Result<PublishedRelease> {
    ensure!(
        assets.x64.id != assets.x86.id,
        "selected x64 and x86 runtime assets must have distinct ids"
    );
    ensure!(
        !assets.x64.name.eq_ignore_ascii_case(&assets.x86.name),
        "selected x64 and x86 runtime assets must have distinct names"
    );
    let actual_assets = response["assets"]
        .as_array()
        .context("GitHub release response has no assets array")?;
    let mut by_id = BTreeMap::new();
    for asset in actual_assets {
        let id = asset["id"]
            .as_u64()
            .context("GitHub release asset id is malformed")?;
        let name = asset["name"]
            .as_str()
            .context("GitHub release asset name is malformed")?;
        let digest = match asset.get("digest") {
            None | Some(serde_json::Value::Null) => None,
            Some(serde_json::Value::String(digest)) => Some(digest.clone()),
            Some(_) => anyhow::bail!("GitHub release asset digest is malformed"),
        };
        let url = asset["browser_download_url"]
            .as_str()
            .context("GitHub release asset download URL is missing")?;
        ensure!(
            by_id
                .insert(id, (name.to_owned(), digest, url.to_owned()))
                .is_none(),
            "GitHub release response repeats an asset id"
        );
    }
    let mut urls = BTreeMap::new();
    for expected in [&assets.x64, &assets.x86] {
        let (name, digest, url) = by_id
            .get(&expected.id)
            .with_context(|| format!("GitHub release has no asset id {}", expected.id))?;
        ensure!(
            name == &expected.name,
            "GitHub runtime asset id has another name"
        );
        let matching_names = actual_assets
            .iter()
            .filter(|asset| {
                asset["name"]
                    .as_str()
                    .is_some_and(|actual| actual.eq_ignore_ascii_case(&expected.name))
            })
            .count();
        ensure!(
            matching_names == 1,
            "GitHub runtime asset name is missing or ambiguous"
        );
        if let Some(digest) = digest {
            let digest = digest
                .strip_prefix("sha256:")
                .context("GitHub asset digest is not SHA256")?;
            ensure!(
                digest.eq_ignore_ascii_case(&expected.sha256),
                "GitHub asset digest differs from the trusted manifest hash"
            );
        }
        validate_asset_url(url, repository, tag, &expected.name)?;
        urls.insert(expected.id, url.clone());
    }
    Ok(PublishedRelease { assets: urls })
}

fn validate_asset_url(url: &str, repository: &str, tag: &str, name: &str) -> Result<()> {
    let direct = format!("https://github.com/{repository}/releases/download/{tag}/{name}");
    let encoded_tag = tag.replace('/', "%2F");
    let encoded = format!("https://github.com/{repository}/releases/download/{encoded_tag}/{name}");
    ensure!(
        url == direct || url == encoded,
        "published runtime asset URL does not match its exact release, tag, and name"
    );
    Ok(())
}

fn stable_version(value: &str) -> Result<String> {
    let version =
        semver::Version::parse(value).context("runtime version is not semantic versioning")?;
    ensure!(
        version.pre.is_empty() && version.build.is_empty(),
        "runtime version must be stable"
    );
    Ok(version.to_string())
}

fn normalized_sha256(value: &str) -> Result<String> {
    validate_sha256(value)?;
    Ok(value.to_ascii_lowercase())
}

fn hash_bytes(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut encoded = String::with_capacity(64);
    for byte in Sha256::digest(bytes) {
        encoded.push(char::from(HEX[usize::from(byte >> 4)]));
        encoded.push(char::from(HEX[usize::from(byte & 0x0f)]));
    }
    encoded
}

fn validate_manifest_fields(value: &serde_json::Value) -> Result<()> {
    let kind = value["kind"]
        .as_str()
        .context("runtime manifest kind is missing")?;
    let allowed: &[&str] = match kind {
        "local" => &["schemaVersion", "kind", "version", "headers", "archives"],
        "published" => &[
            "schemaVersion",
            "kind",
            "version",
            "headers",
            "repository",
            "releaseId",
            "tag",
            "assets",
        ],
        _ => anyhow::bail!("unsupported runtime manifest kind '{kind}'"),
    };
    let object = value
        .as_object()
        .context("runtime manifest must be a JSON object")?;
    ensure!(
        object.keys().all(|key| allowed.contains(&key.as_str())),
        "runtime manifest contains an unsupported top-level field"
    );
    Ok(())
}

fn validate_repository(value: &str) -> Result<()> {
    let parts = value.split('/').collect::<Vec<_>>();
    ensure!(
        parts.len() == 2,
        "runtime repository must be an explicit owner/repository identity"
    );
    for part in parts {
        ensure!(
            !part.is_empty()
                && part.len() <= 100
                && part != "."
                && part != ".."
                && part
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.')),
            "runtime repository identity contains an invalid component"
        );
    }
    Ok(())
}

fn validate_tag(tag: &str) -> Result<()> {
    ensure!(
        !tag.is_empty()
            && tag.len() <= 256
            && !tag.contains("..")
            && !tag.ends_with('.')
            && !Path::new(tag)
                .extension()
                .is_some_and(|extension| extension.eq_ignore_ascii_case("lock"))
            && !tag.contains("@{")
            && tag.split('/').all(|part| {
                !part.is_empty()
                    && part != "."
                    && part != ".."
                    && part.bytes().all(|byte| {
                        byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b'/')
                    })
            }),
        "runtime release tag is not canonical"
    );
    Ok(())
}

fn validate_asset_name(name: &str) -> Result<()> {
    ensure!(
        !name.is_empty()
            && name.len() <= 255
            && name != "."
            && name != ".."
            && name
                .bytes()
                .all(|byte| { byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.') }),
        "runtime asset name is not a canonical filename"
    );
    Ok(())
}

fn tool_environment(toolset: &Toolset) -> BTreeMap<OsString, OsString> {
    toolset
        .environment()
        .iter()
        .map(|(key, value)| (OsString::from(key), OsString::from(value)))
        .collect()
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct RuntimeManifest {
    schema_version: u32,
    #[serde(flatten)]
    pair: RuntimeManifestKind,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(tag = "kind", rename_all = "kebab-case", deny_unknown_fields)]
enum RuntimeManifestKind {
    Local {
        version: String,
        headers: RuntimeHeaders,
        archives: RuntimeInputArchives,
    },
    Published {
        version: String,
        headers: RuntimeHeaders,
        repository: String,
        #[serde(rename = "releaseId")]
        release_id: u64,
        tag: String,
        assets: RuntimePublishedAssets,
    },
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct RuntimeHeaders {
    core_sha256: String,
    effects_sha256: String,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct RuntimeInputArchives {
    x64: RuntimeLocalArchive,
    x86: RuntimeLocalArchive,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct RuntimeLocalArchive {
    path: String,
    sha256: String,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct RuntimePublishedAssets {
    x64: RuntimePublishedAsset,
    x86: RuntimePublishedAsset,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct RuntimePublishedAsset {
    id: u64,
    name: String,
    sha256: String,
}

#[derive(Clone, Debug)]
struct PublishedRelease {
    assets: BTreeMap<u64, String>,
}

#[cfg(test)]
mod tests {
    use super::{validate_asset_url, validate_repository, validate_tag};

    #[test]
    fn release_identity_and_asset_urls_are_exact() {
        assert!(validate_repository("reviewed-owner/runtime-repo").is_ok());
        assert!(validate_tag("foundation/v0.1.0").is_ok());
        assert!(validate_asset_url(
            "https://github.com/reviewed-owner/runtime-repo/releases/download/foundation/v0.1.0/x64.zip",
            "reviewed-owner/runtime-repo",
            "foundation/v0.1.0",
            "x64.zip"
        )
        .is_ok());
        assert!(validate_asset_url(
            "https://evilgithub.com/reviewed-owner/runtime-repo/releases/download/foundation/v0.1.0/x64.zip",
            "reviewed-owner/runtime-repo",
            "foundation/v0.1.0",
            "x64.zip"
        )
        .is_err());
        assert!(validate_repository("https://github.com/owner/repo").is_err());
        assert!(validate_repository("owner/../repo").is_err());
        assert!(validate_tag("v0.1.0/../other").is_err());
        assert!(validate_tag("v0.1.0?redirect=other").is_err());
    }
}
