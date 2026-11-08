//! Deterministic package ZIP creation and exact payload inventory checks.

use std::collections::{BTreeMap, BTreeSet};
use std::ffi::OsStr;
use std::fs::{self, File};
use std::io::Write;
use std::path::{Component, Path};

use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use zip::write::SimpleFileOptions;
use zip::{CompressionMethod, ZipWriter};

use super::{PackageExpectation, PackageKind, pe};
use crate::model::{Architecture, Product};
use crate::process::Runner;
use crate::tooling::hash_file;

const MAX_MANIFEST_BYTES: u64 = 16 * 1024 * 1024;
const MAX_PACKAGE_FILE_BYTES: u64 = 2 * 1024 * 1024 * 1024;

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct PackageManifest {
    schema_version: u32,
    product: Product,
    architecture: Option<Architecture>,
    version: String,
    kind: PackageKind,
    source: ManifestSourceIdentity,
    files: Vec<ManifestFile>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct ManifestSourceIdentity {
    commit: Option<String>,
    modified: bool,
    snapshot_sha256: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
struct ManifestFile {
    path: String,
    size: u64,
    sha256: String,
}

#[derive(Debug)]
pub(super) struct ExtractedPackage {
    pub entry_count: usize,
    pub pe_machine: Option<String>,
    pub exports: Vec<String>,
}

pub(super) fn package_filename(expectation: &PackageExpectation) -> String {
    match expectation.kind {
        PackageKind::Source => format!(
            "InputNextAdapters-{}-{}-source.zip",
            expectation.product.as_str(),
            expectation.version
        ),
        PackageKind::Binary => format!(
            "InputNextAdapters-{}-{}-windows-{}.zip",
            expectation.product.as_str(),
            expectation.version,
            expectation
                .architecture
                .map_or("none", Architecture::as_str)
        ),
        PackageKind::Symbols => format!(
            "InputNextAdapters-{}-{}-windows-{}-symbols.zip",
            expectation.product.as_str(),
            expectation.version,
            expectation
                .architecture
                .map_or("none", Architecture::as_str)
        ),
    }
}

pub(super) fn write_package_archive(
    stage: &Path,
    destination: &Path,
    expectation: &PackageExpectation,
) -> Result<()> {
    let files = enumerate_files(stage)?;
    ensure!(!files.is_empty(), "package payload cannot be empty");
    ensure!(
        files
            .iter()
            .all(|(path, _, _)| !path.eq_ignore_ascii_case("manifest.json")),
        "package staging payload cannot supply its own manifest"
    );
    validate_payload_shape(stage, expectation, &files)?;
    let manifest = PackageManifest {
        schema_version: 1,
        product: expectation.product,
        architecture: expectation.architecture,
        version: expectation.version.clone(),
        kind: expectation.kind,
        source: ManifestSourceIdentity {
            commit: expectation.source.commit.clone(),
            modified: expectation.source.modified,
            snapshot_sha256: expectation.source.snapshot_sha256.clone(),
        },
        files: files
            .iter()
            .map(|(path, size, sha256)| ManifestFile {
                path: path.clone(),
                size: *size,
                sha256: sha256.clone(),
            })
            .collect(),
    };
    let manifest_bytes = serde_json::to_vec_pretty(&manifest)
        .context("could not encode package content manifest")?;
    ensure!(
        manifest_bytes.len() as u64 <= MAX_MANIFEST_BYTES,
        "package content manifest exceeds its size limit"
    );
    let destination_file = File::create(destination)
        .with_context(|| format!("could not create package archive {}", destination.display()))?;
    let mut zip = ZipWriter::new(destination_file);
    let options = SimpleFileOptions::default()
        .compression_method(CompressionMethod::Deflated)
        .unix_permissions(0o644);
    for (relative, _, _) in files {
        let source = stage.join(Path::new(&relative));
        zip.start_file(&relative, options)
            .with_context(|| format!("could not add {relative} to package archive"))?;
        let mut input = File::open(&source)
            .with_context(|| format!("could not open package file {}", source.display()))?;
        std::io::copy(&mut input, &mut zip)
            .with_context(|| format!("could not archive package file {relative}"))?;
    }
    zip.start_file("manifest.json", options)
        .context("could not add package content manifest")?;
    zip.write_all(&manifest_bytes)
        .context("could not write package content manifest")?;
    let file = zip
        .finish()
        .context("could not finish package ZIP archive")?;
    file.sync_all()
        .context("could not flush package ZIP archive")?;
    Ok(())
}

pub(super) fn inspect_extracted_package(
    root: &Path,
    expectation: &PackageExpectation,
) -> Result<ExtractedPackage> {
    let manifest_path = root.join("manifest.json");
    let metadata =
        fs::symlink_metadata(&manifest_path).context("package archive has no manifest.json")?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= MAX_MANIFEST_BYTES,
        "package manifest must be a bounded regular file"
    );
    let manifest_bytes = fs::read(&manifest_path).context("could not read package manifest")?;
    let manifest: PackageManifest =
        crate::json::parse(&manifest_bytes).context("package manifest is not valid strict JSON")?;
    ensure!(
        manifest.schema_version == 1,
        "unsupported package manifest schema"
    );
    ensure!(
        manifest.product == expectation.product
            && manifest.architecture == expectation.architecture
            && manifest.version == expectation.version
            && manifest.kind == expectation.kind,
        "package manifest identity does not match the expected product package"
    );
    ensure!(
        manifest.source.commit == expectation.source.commit
            && manifest.source.modified == expectation.source.modified
            && manifest.source.snapshot_sha256 == expectation.source.snapshot_sha256,
        "package manifest source identity does not match the expected source"
    );
    let files = enumerate_files(root)?;
    ensure!(
        files.iter().any(|(path, _, _)| path == "manifest.json"),
        "package manifest is missing from the extracted inventory"
    );
    let actual = files
        .into_iter()
        .filter(|(path, _, _)| path != "manifest.json")
        .collect::<Vec<_>>();
    ensure!(
        manifest.files.iter().map(|file| &file.path).is_sorted(),
        "package manifest inventory must be sorted by path"
    );
    ensure!(
        manifest.files.len() == actual.len(),
        "package manifest inventory is incomplete or contains extras"
    );
    for (declared, (actual_path, actual_size, actual_hash)) in manifest.files.iter().zip(&actual) {
        ensure!(
            declared.path == *actual_path
                && declared.size == *actual_size
                && declared.sha256 == *actual_hash,
            "package inventory mismatch for '{actual_path}'"
        );
    }
    validate_payload_shape(root, expectation, &actual)?;
    let (pe_machine, exports) = match expectation.kind {
        PackageKind::Binary => match plugin_relative_path(expectation.product) {
            Some(relative) => {
                let path = root.join(relative);
                let inspected = pe::inspect(
                    &path,
                    expectation
                        .architecture
                        .context("plugin package requires an architecture")?,
                )?;
                (
                    Some(inspected.machine),
                    inspected.exports.into_iter().collect(),
                )
            }
            None => (None, Vec::new()),
        },
        PackageKind::Symbols | PackageKind::Source => (None, Vec::new()),
    };
    Ok(ExtractedPackage {
        entry_count: manifest.files.len() + 1,
        pe_machine,
        exports,
    })
}

fn enumerate_files(root: &Path) -> Result<Vec<(String, u64, String)>> {
    let root = dunce::canonicalize(root).context("could not resolve package staging root")?;
    let mut files = BTreeMap::new();
    let mut folded = BTreeSet::new();
    for entry in walkdir::WalkDir::new(&root)
        .follow_links(false)
        .min_depth(1)
    {
        let entry = entry.context("could not enumerate package staging tree")?;
        let metadata = fs::symlink_metadata(entry.path())?;
        ensure!(
            !metadata.file_type().is_symlink(),
            "package payload contains a link"
        );
        if metadata.is_dir() {
            continue;
        }
        ensure!(
            metadata.is_file(),
            "package payload contains a non-file filesystem entry"
        );
        ensure!(
            metadata.len() <= MAX_PACKAGE_FILE_BYTES,
            "package file exceeds its size limit"
        );
        let relative = entry.path().strip_prefix(&root)?;
        let path = normalized_relative_path(relative)?;
        let lower = path.to_ascii_lowercase();
        ensure!(
            folded.insert(lower),
            "package payload contains case-colliding paths"
        );
        let hash = hash_file(entry.path())?;
        files.insert(path, (metadata.len(), hash));
    }
    Ok(files
        .into_iter()
        .map(|(path, (size, hash))| (path, size, hash))
        .collect())
}

fn normalized_relative_path(path: &Path) -> Result<String> {
    ensure!(
        path.components()
            .all(|part| matches!(part, Component::Normal(_))),
        "package path is not normalized"
    );
    let mut value = String::new();
    for component in path.components() {
        let Component::Normal(part) = component else {
            anyhow::bail!("package path is not normalized");
        };
        let part = part.to_str().context("package path is not valid Unicode")?;
        ensure!(
            !part.is_empty()
                && !part.contains(['/', '\\', ':', '\0'])
                && part != "."
                && part != "..",
            "package path has a forbidden component"
        );
        if !value.is_empty() {
            value.push('/');
        }
        value.push_str(part);
    }
    ensure!(!value.is_empty(), "package path is empty");
    Ok(value)
}

fn validate_payload_shape(
    root: &Path,
    expectation: &PackageExpectation,
    files: &[(String, u64, String)],
) -> Result<()> {
    let file_map = files
        .iter()
        .map(|(path, size, _)| (path.as_str(), *size))
        .collect::<BTreeMap<_, _>>();
    match expectation.kind {
        PackageKind::Binary => match expectation.product {
            Product::Foundation => {
                for required in [
                    "lib/inputnext_adapters_runtime_client.lib",
                    "lib/inputnext_adapters_effects.lib",
                    "include/inputnext_adapters/sdk/input_next_core.h",
                    "include/inputnext_adapters/sdk/input_next_effects.h",
                ] {
                    ensure!(
                        file_map.contains_key(required),
                        "foundation package is missing {required}"
                    );
                }
                ensure!(
                    files
                        .iter()
                        .all(|(path, _, _)| !forbidden_game_payload(path)),
                    "foundation binary package contains a runtime or game redistributable"
                );
            }
            product => {
                for required in required_adapter_payload(product) {
                    match file_map.get(required) {
                        Some(&size) => ensure!(
                            size > 0,
                            "adapter package payload file '{required}' must not be empty"
                        ),
                        None => anyhow::bail!("adapter package is missing {required}"),
                    }
                }
                ensure!(
                    files
                        .iter()
                        .all(|(path, _, _)| !forbidden_plugin_payload(path)),
                    "plugin package contains the InputNext runtime or GameInput redistributable"
                );
                let plugin = plugin_relative_path(product)
                    .context("adapter binary package has no known plugin path")?;
                let pe = pe::inspect(
                    &root.join(plugin),
                    expectation
                        .architecture
                        .context("plugin package requires an architecture")?,
                )?;
                validate_plugin_exports(product, &pe.exports)?;
            }
        },
        PackageKind::Symbols => {
            ensure!(
                files
                    .iter()
                    .any(|(path, _, _)| path.to_ascii_lowercase().ends_with(".pdb")),
                "symbol package contains no PDB files"
            );
            ensure!(
                files
                    .iter()
                    .all(|(path, _, _)| !path.to_ascii_lowercase().ends_with(".dll")),
                "symbol package must not contain DLL payloads"
            );
        }
        PackageKind::Source => {
            ensure!(
                files.iter().all(|(path, _, _)| !source_forbidden(path)),
                "source package contains Git or build output"
            );
            if matches!(expectation.product, Product::SkyrimSe | Product::Fallout4) {
                ensure!(
                    file_map.contains_key("inputnext-adapters-source.json"),
                    "source bundle is missing its dependency source manifest"
                );
            }
        }
    }
    Ok(())
}

pub(super) fn plugin_relative_path(product: Product) -> Option<&'static str> {
    match product {
        Product::Foundation => None,
        Product::SkyrimSe => Some("Data/SKSE/Plugins/InputNextSkyrim.dll"),
        Product::SkyrimLe => Some("Data/SKSE/Plugins/InputNextSkyrimLE.dll"),
        Product::Fallout4 => Some("Data/F4SE/Plugins/InputNextFallout4.dll"),
    }
}

pub(super) fn validate_source_matches_data_root(
    package_root: &Path,
    expectation: &PackageExpectation,
    source_root: &Path,
    runner: &mut dyn Runner,
) -> Result<()> {
    let package_files = enumerate_files(package_root)?
        .into_iter()
        .filter(|(path, _, _)| path != "manifest.json")
        .map(|(path, size, sha256)| (path, (size, sha256)))
        .collect::<BTreeMap<_, _>>();
    let package_authored = package_files
        .iter()
        .filter(|(path, _)| !is_source_bundle_extra(path, expectation.product))
        .map(|(path, value)| (path.clone(), value.clone()))
        .collect::<BTreeMap<_, _>>();
    let source_root = dunce::canonicalize(source_root).with_context(|| {
        format!(
            "could not resolve source data root {}",
            source_root.display()
        )
    })?;
    let source_paths = if source_root.join(".git").exists() {
        super::dependencies::authored_source_paths(&source_root, runner)?
    } else {
        walk_authored_source(&source_root)?
    };
    let mut source_files = BTreeMap::new();
    for relative in source_paths {
        let path = source_root.join(&relative);
        let metadata = fs::symlink_metadata(&path)
            .with_context(|| format!("could not inspect authored source {}", path.display()))?;
        ensure!(
            metadata.is_file() && !metadata.file_type().is_symlink(),
            "authored source contains a non-regular file: {relative}"
        );
        source_files.insert(relative, (metadata.len(), hash_file(&path)?));
    }
    ensure!(
        source_files == package_authored,
        "source package authored file paths or SHA-256 hashes differ from qualified source data"
    );
    Ok(())
}

fn is_source_bundle_extra(path: &str, product: Product) -> bool {
    if path == "inputnext-adapters-source.json" {
        return matches!(product, Product::SkyrimSe | Product::Fallout4);
    }
    match product {
        Product::SkyrimSe => path.starts_with("dependencies/skyrim-se/"),
        Product::Fallout4 => path.starts_with("dependencies/fallout4/"),
        Product::Foundation | Product::SkyrimLe => false,
    }
}

fn walk_authored_source(root: &Path) -> Result<Vec<String>> {
    let mut paths = Vec::new();
    let mut folded = BTreeSet::new();
    for entry in walkdir::WalkDir::new(root).follow_links(false).min_depth(1) {
        let entry = entry.context("could not walk audited source data")?;
        let relative = entry.path().strip_prefix(root)?;
        if excluded_authored_source(relative) {
            continue;
        }
        let metadata = fs::symlink_metadata(entry.path())?;
        ensure!(
            !metadata.file_type().is_symlink(),
            "audited source data contains a link"
        );
        if metadata.is_dir() {
            continue;
        }
        ensure!(
            metadata.is_file(),
            "audited source data contains a non-file entry"
        );
        let path = normalized_relative_path(relative)?;
        ensure!(
            folded.insert(path.to_ascii_lowercase()),
            "audited source data has case aliases"
        );
        paths.push(path);
    }
    paths.sort();
    Ok(paths)
}

fn excluded_authored_source(path: &Path) -> bool {
    path.components().any(|component| {
        let Component::Normal(name) = component else {
            return false;
        };
        let name = name.to_string_lossy().to_ascii_lowercase();
        name == ".git"
            || name == "build"
            || name == "target"
            || name == "cmakefiles"
            || name == "_deps"
            || name.starts_with("cmake-build-")
    }) || matches!(
        path.file_name().and_then(|name| name.to_str()),
        Some("CMakeCache.txt" | "cmake_install.cmake" | "build.ninja" | "compile_commands.json")
    )
}

fn required_adapter_payload(product: Product) -> &'static [&'static str] {
    match product {
        Product::Foundation => &[],
        Product::Fallout4 => &[
            "Data/F4SE/Plugins/InputNextFallout4.dll",
            "Data/F4SE/Plugins/InputNextFallout4.ini",
            "Data/F4SE/Plugins/InputNextFallout4.reference.ini",
            "README.md",
            "licenses/CommonLibF4-EXCEPTIONS.txt",
            "licenses/CommonLibF4-GPL-3.0.txt",
            "licenses/CommonLibShared-EXCEPTIONS.txt",
            "licenses/CommonLibShared-GPL-3.0.txt",
            "licenses/InputNext-MIT.txt",
            "licenses/THIRD_PARTY_NOTICES",
            "licenses/spdlog-MIT.txt",
        ],
        Product::SkyrimSe => &[
            "Data/SKSE/Plugins/InputNextSkyrim.dll",
            "Data/SKSE/Plugins/InputNextSkyrim.ini",
            "Data/SKSE/Plugins/InputNextSkyrim.reference.ini",
            "README.md",
            "licenses/CommonLibSSE-COPYING.txt",
            "licenses/CommonLibSSE-EXCEPTIONS.md",
            "licenses/CommonLibSSE-MIT.txt",
            "licenses/DirectXTK-LICENSE",
            "licenses/InputNext-MIT.txt",
            "licenses/THIRD_PARTY_NOTICES",
            "licenses/fmt-LICENSE",
            "licenses/rapidcsv-LICENSE",
            "licenses/spdlog-LICENSE",
        ],
        Product::SkyrimLe => &[
            "Data/SKSE/Plugins/InputNextSkyrimLE.dll",
            "Data/SKSE/Plugins/InputNextSkyrimLE.ini",
            "Data/SKSE/Plugins/InputNextSkyrimLE.reference.ini",
            "README.md",
            "licenses/InputNext-MIT.txt",
            "licenses/SKSE-source-terms.txt",
            "licenses/THIRD_PARTY_NOTICES",
        ],
    }
}

fn required_exports(product: Product) -> &'static [&'static str] {
    match product {
        Product::Foundation => &[],
        Product::SkyrimSe => &["SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"],
        Product::SkyrimLe => &["SKSEPlugin_Load", "SKSEPlugin_Query"],
        Product::Fallout4 => &["F4SEPlugin_Load", "F4SEPlugin_Version"],
    }
}

fn validate_plugin_exports(product: Product, actual: &[String]) -> Result<()> {
    let expected = required_exports(product);
    ensure!(
        actual
            .iter()
            .map(String::as_str)
            .eq(expected.iter().copied()),
        "plugin PE named exports differ from the exact required set"
    );
    Ok(())
}

fn forbidden_game_payload(path: &str) -> bool {
    let path = Path::new(path);
    let file_name = path.file_name().and_then(OsStr::to_str);
    let extension = path.extension().and_then(OsStr::to_str);
    let is_game_binary = extension.is_some_and(|extension| {
        extension.eq_ignore_ascii_case("dll") || extension.eq_ignore_ascii_case("exe")
    }) || file_name
        .is_some_and(|name| name.eq_ignore_ascii_case(".dll") || name.eq_ignore_ascii_case(".exe"));
    is_game_binary
        || path
            .to_string_lossy()
            .to_ascii_lowercase()
            .contains("gameinput")
}

fn forbidden_plugin_payload(path: &str) -> bool {
    let folded = path.to_ascii_lowercase();
    folded.ends_with("/inputnext_runtime.dll")
        || folded.ends_with("/gameinputredist.dll")
        || folded.ends_with("/gameinputrawinputproxy.exe")
        || folded.ends_with("/xinput1_3.dll")
        || folded.ends_with("/xinput1_4.dll")
        || folded.ends_with("/xinput9_1_0.dll")
}

fn source_forbidden(path: &str) -> bool {
    let folded = path.to_ascii_lowercase();
    let under_dependency_bundle = folded.starts_with("dependencies/");
    folded.split('/').any(|part| {
        matches!(part, ".git" | "target" | "cmakefiles" | "_deps")
            || (!under_dependency_bundle && part == "build")
            || part.starts_with("cmake-build-")
    }) || matches!(
        Path::new(path).file_name().and_then(|name| name.to_str()),
        Some("CMakeCache.txt" | "cmake_install.cmake" | "build.ninja" | "compile_commands.json")
    )
}

#[cfg(test)]
mod tests;
