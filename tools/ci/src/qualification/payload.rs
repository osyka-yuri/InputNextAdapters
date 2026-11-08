use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom};
use std::path::{Component, Path, PathBuf};

use anyhow::{Context, Result, ensure};
use serde::Deserialize;

use crate::model::Architecture;
use crate::tooling::{ArchiveFormat, extract_archive, hash_file, inspect_archive, validate_sha256};

use super::input::{RuntimeArchiveInput, RuntimePairInput};
use super::{RuntimePairOrigin, VerifiedRuntimeArchive, VerifiedRuntimePair};

const MAX_RUNTIME_METADATA_BYTES: u64 = 16 * 1024 * 1024;
const MAX_PE_BYTES: u64 = 1024 * 1024 * 1024;

pub(super) fn finish_runtime_pair(
    input: RuntimePairInput,
    data_root: &Path,
    context: &crate::model::ExecutionContext,
) -> Result<VerifiedRuntimePair> {
    let core_source = source_sdk_header(data_root, "input_next_core.h")?;
    let effects_source = source_sdk_header(data_root, "input_next_effects.h")?;
    ensure!(
        hash_file(&core_source)? == input.core_sha256,
        "runtime Core header hash does not match the source-tag SDK header"
    );
    ensure!(
        hash_file(&effects_source)? == input.effects_sha256,
        "runtime Effects header hash does not match the source-tag SDK header"
    );

    let prepared = [
        prepare_archive(&input.archives[0])?,
        prepare_archive(&input.archives[1])?,
    ];
    ensure!(
        prepared[0].sha256 != prepared[1].sha256,
        "runtime x64 and x86 archive hashes must identify distinct packages"
    );

    let output = dunce::canonicalize(&context.output).with_context(|| {
        format!(
            "could not resolve qualification output {}",
            context.output.display()
        )
    })?;
    ensure!(
        context.output.is_absolute() && output == context.output,
        "qualification output must be an absolute canonical directory"
    );
    let runtimes = create_child_directory(&output, "runtimes")?;
    let extraction_root = runtimes.join(&input.version);
    fs::create_dir(&extraction_root).with_context(|| {
        format!(
            "could not create fresh runtime extraction directory {}",
            extraction_root.display()
        )
    })?;
    let extraction_root = dunce::canonicalize(&extraction_root)?;
    ensure!(
        extraction_root.starts_with(&runtimes),
        "runtime extraction directory escaped the qualification output"
    );

    let x64 = inspect_runtime_archive(
        &prepared[0],
        &extraction_root,
        &core_source,
        &effects_source,
        &input.version,
        &input.core_sha256,
        &input.effects_sha256,
    )?;
    let x86 = inspect_runtime_archive(
        &prepared[1],
        &extraction_root,
        &core_source,
        &effects_source,
        &input.version,
        &input.core_sha256,
        &input.effects_sha256,
    )?;
    ensure!(
        x64.architecture == Architecture::X64 && x86.architecture == Architecture::X86,
        "runtime archive architectures are not in canonical pair order"
    );

    Ok(VerifiedRuntimePair {
        schema_version: 1,
        origin: input.origin,
        repository: input.repository,
        release_id: input.release_id,
        tag: input.tag,
        version: input.version,
        core_sha256: input.core_sha256,
        effects_sha256: input.effects_sha256,
        manifest_sha256: input.manifest_sha256,
        x64,
        x86,
    })
}

pub(super) fn verify_pair(pair: &VerifiedRuntimePair, output: &Path) -> Result<()> {
    ensure!(
        pair.schema_version == 1,
        "unsupported verified runtime-pair schema"
    );
    let version =
        semver::Version::parse(&pair.version).context("runtime pair version is malformed")?;
    ensure!(
        version.pre.is_empty() && version.build.is_empty() && version.to_string() == pair.version,
        "runtime pair version must be canonical stable semantic versioning"
    );
    validate_sha256(&pair.core_sha256)?;
    validate_sha256(&pair.effects_sha256)?;
    validate_sha256(&pair.manifest_sha256)?;
    validate_origin(pair)?;

    ensure!(
        output.is_absolute(),
        "qualification output must be absolute"
    );
    let output = dunce::canonicalize(output).context("could not resolve qualification output")?;
    let recheck_root = tempfile::tempdir_in(&output)
        .context("could not create isolated runtime revalidation directory")?;
    for (architecture, archive) in [
        (Architecture::X64, &pair.x64),
        (Architecture::X86, &pair.x86),
    ] {
        ensure!(
            archive.architecture == architecture,
            "verified runtime archive architecture does not match its pair slot"
        );
        validate_sha256(&archive.sha256)?;
        ensure!(
            archive.size_bytes > 0,
            "verified runtime archive has an empty size"
        );
        let path = require_regular_file(&archive.path, "verified runtime archive")?;
        ensure!(
            hash_file(&path)? == archive.sha256,
            "verified runtime archive changed after static validation"
        );
        ensure!(
            fs::metadata(&path)?.len() == archive.size_bytes,
            "verified runtime archive size changed after static validation"
        );
        let bin = dunce::canonicalize(&archive.bin_directory)
            .context("could not resolve verified runtime bin directory")?;
        ensure!(
            bin.is_dir()
                && bin == archive.bin_directory
                && bin.file_name().is_some_and(|name| name == "bin")
                && bin.starts_with(&output),
            "verified runtime bin directory is outside the qualification output"
        );
        let extraction_root = bin
            .parent()
            .context("runtime bin directory has no parent")?;
        validate_runtime_contents(
            extraction_root,
            architecture,
            &pair.version,
            &pair.core_sha256,
            &pair.effects_sha256,
        )?;
        let extracted_files = enumerate_runtime_files(extraction_root)?;
        let recheck_path = recheck_root.path().join(architecture.as_str());
        inspect_archive(&path, ArchiveFormat::Zip)?;
        extract_archive(&path, ArchiveFormat::Zip, &recheck_path)?;
        ensure!(
            hash_file(&path)? == archive.sha256,
            "verified runtime archive changed during revalidation"
        );
        validate_runtime_contents(
            &recheck_path,
            architecture,
            &pair.version,
            &pair.core_sha256,
            &pair.effects_sha256,
        )?;
        ensure!(
            extracted_files == enumerate_runtime_files(&recheck_path)?,
            "verified runtime extraction does not match its archive bytes"
        );
    }
    ensure!(
        pair.x64.sha256 != pair.x86.sha256,
        "verified runtime pair reuses the same archive for both architectures"
    );
    Ok(())
}

fn validate_origin(pair: &VerifiedRuntimePair) -> Result<()> {
    match pair.origin {
        RuntimePairOrigin::Local => {
            ensure!(
                pair.repository.is_none()
                    && pair.release_id.is_none()
                    && pair.tag.is_none()
                    && pair.x64.asset_id.is_none()
                    && pair.x64.asset_name.is_none()
                    && pair.x86.asset_id.is_none()
                    && pair.x86.asset_name.is_none(),
                "local runtime pair contains published-release identity"
            );
            Ok(())
        }
        RuntimePairOrigin::Published => {
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
            ensure!(release_id > 0, "published runtime pair release id is zero");
            super::input::validate_published_identity(repository, tag, &pair.version)?;
            ensure!(
                pair.x64.asset_id.is_some_and(|id| id > 0)
                    && pair.x86.asset_id.is_some_and(|id| id > 0)
                    && pair.x64.asset_id != pair.x86.asset_id,
                "published runtime pair asset ids are missing or duplicated"
            );
            ensure!(
                pair.x64
                    .asset_name
                    .as_deref()
                    .is_some_and(|name| !name.is_empty())
                    && pair
                        .x86
                        .asset_name
                        .as_deref()
                        .is_some_and(|name| !name.is_empty())
                    && pair.x64.asset_name != pair.x86.asset_name,
                "published runtime pair asset names are missing or duplicated"
            );
            Ok(())
        }
    }
}

fn prepare_archive(input: &RuntimeArchiveInput) -> Result<PreparedArchive> {
    let path = require_regular_file(&input.path, "runtime archive")?;
    let metadata = fs::metadata(&path)?;
    ensure!(metadata.len() > 0, "runtime archive is empty");
    validate_sha256(&input.sha256)?;
    inspect_archive(&path, ArchiveFormat::Zip)?;
    let sha256 = hash_file(&path)?;
    ensure!(sha256 == input.sha256, "runtime archive SHA-256 mismatch");
    Ok(PreparedArchive {
        input: input.clone(),
        path,
        sha256,
        size_bytes: metadata.len(),
    })
}

fn inspect_runtime_archive(
    archive: &PreparedArchive,
    extraction_root: &Path,
    core_source: &Path,
    effects_source: &Path,
    version: &str,
    core_sha256: &str,
    effects_sha256: &str,
) -> Result<VerifiedRuntimeArchive> {
    let architecture = archive.input.architecture;
    let destination = extraction_root.join(architecture.as_str());
    extract_archive(&archive.path, ArchiveFormat::Zip, &destination)?;
    ensure!(
        hash_file(&archive.path)? == archive.sha256,
        "runtime archive changed during extraction"
    );
    let destination = dunce::canonicalize(&destination)?;
    ensure!(
        destination.starts_with(extraction_root),
        "runtime extraction directory escaped the verified extraction root"
    );
    validate_runtime_contents(
        &destination,
        architecture,
        version,
        core_sha256,
        effects_sha256,
    )?;
    verify_identical_sdk_headers(&destination, core_source, effects_source)?;
    let bin_directory = dunce::canonicalize(destination.join("bin"))
        .context("runtime archive has no bin directory")?;
    ensure!(
        bin_directory.starts_with(&destination) && bin_directory.is_dir(),
        "runtime bin directory escaped the verified extraction root"
    );
    Ok(VerifiedRuntimeArchive {
        architecture,
        path: archive.path.clone(),
        sha256: archive.sha256.clone(),
        size_bytes: archive.size_bytes,
        asset_id: archive.input.asset_id,
        asset_name: archive.input.asset_name.clone(),
        bin_directory,
    })
}

fn validate_runtime_contents(
    root: &Path,
    architecture: Architecture,
    expected_version: &str,
    core_sha256: &str,
    effects_sha256: &str,
) -> Result<()> {
    let manifest = read_runtime_package_manifest(root)?;
    let files = validate_runtime_inventory(root, &manifest)?;
    validate_sdk_header_hashes(&manifest.files, core_sha256, effects_sha256)?;
    validate_runtime_build_info(root, architecture, expected_version)?;
    validate_runtime_payloads(root, &files, architecture)
}

fn read_runtime_package_manifest(root: &Path) -> Result<RuntimePackageManifest> {
    let manifest_path = root.join("manifest.json");
    let metadata = regular_file_metadata(&manifest_path, "runtime content manifest")?;
    ensure!(
        metadata.len() <= MAX_RUNTIME_METADATA_BYTES,
        "runtime manifest exceeds the size limit"
    );
    let manifest: RuntimePackageManifest = crate::json::parse(&fs::read(&manifest_path)?)
        .context("runtime package manifest is malformed")?;
    ensure!(
        manifest.schema == 1,
        "unsupported runtime package manifest schema"
    );
    Ok(manifest)
}

fn validate_runtime_inventory(
    root: &Path,
    manifest: &RuntimePackageManifest,
) -> Result<BTreeMap<String, String>> {
    let files = enumerate_runtime_files(root)?;
    let declared_paths = manifest.files.keys().cloned().collect::<BTreeSet<_>>();
    let actual_paths = files
        .keys()
        .filter(|path| *path != "manifest.json")
        .cloned()
        .collect::<BTreeSet<_>>();
    ensure!(
        !declared_paths.is_empty() && declared_paths == actual_paths,
        "runtime package content manifest is incomplete or contains extra files"
    );
    let mut declared_folded = BTreeSet::new();
    for (path, expected_hash) in &manifest.files {
        validate_manifest_path(path)?;
        ensure!(
            declared_folded.insert(path.to_ascii_lowercase()),
            "runtime package manifest contains case aliases"
        );
        validate_sha256(expected_hash)?;
        ensure!(
            files
                .get(path)
                .is_some_and(|actual| actual.eq_ignore_ascii_case(expected_hash)),
            "runtime package hash mismatch for {path}"
        );
    }
    Ok(files)
}

fn validate_sdk_header_hashes(
    declared_files: &BTreeMap<String, String>,
    core_sha256: &str,
    effects_sha256: &str,
) -> Result<()> {
    let core_path = "sdk/input_next_core.h";
    let effects_path = "sdk/input_next_effects.h";
    ensure!(
        declared_files
            .get(core_path)
            .is_some_and(|hash| hash.eq_ignore_ascii_case(core_sha256)),
        "runtime Core SDK header does not match the qualified source-tag header"
    );
    ensure!(
        declared_files
            .get(effects_path)
            .is_some_and(|hash| hash.eq_ignore_ascii_case(effects_sha256)),
        "runtime Effects SDK header does not match the qualified source-tag header"
    );

    Ok(())
}

fn validate_runtime_build_info(
    root: &Path,
    architecture: Architecture,
    expected_version: &str,
) -> Result<()> {
    let build_info_path = root.join("build-info.json");
    let metadata = regular_file_metadata(&build_info_path, "runtime build metadata")?;
    ensure!(
        metadata.len() <= MAX_RUNTIME_METADATA_BYTES,
        "runtime build metadata exceeds the size limit"
    );
    let build_info: RuntimeBuildInfo = crate::json::parse(&fs::read(&build_info_path)?)
        .context("runtime build metadata is malformed")?;
    let expected_arch = architecture.as_str();
    ensure!(
        build_info.name == "InputNext"
            && build_info.architecture == expected_arch
            && build_info.configuration == "Release"
            && build_info.optimization == "LTCG"
            && matches!(build_info.pgo.as_str(), "Off" | "Optimize")
            && build_info.source_modified == Some(false),
        "runtime archive is not a clean production package for {expected_arch}"
    );
    let version = semver::Version::parse(&build_info.version)
        .context("runtime build metadata version is not semantic versioning")?;
    ensure!(
        version.pre.is_empty()
            && version.build.is_empty()
            && version.to_string() == expected_version,
        "runtime archive version does not match its qualified pair"
    );
    ensure!(
        !build_info.compiler.is_empty()
            && !build_info.gameinput.is_empty()
            && !build_info.tomlplusplus.is_empty()
            && !build_info.source_revision.is_empty(),
        "runtime build metadata is incomplete"
    );

    Ok(())
}

fn validate_runtime_payloads(
    root: &Path,
    files: &BTreeMap<String, String>,
    architecture: Architecture,
) -> Result<()> {
    let core_path = "sdk/input_next_core.h";
    let effects_path = "sdk/input_next_effects.h";
    for required in [
        "bin/GameInputRedist.dll",
        "bin/GameInputRawInputProxy.exe",
        "bin/inputnext_runtime.dll",
        "bin/xinput1_3.dll",
        "bin/xinput1_4.dll",
        "bin/xinput9_1_0.dll",
        core_path,
        effects_path,
    ] {
        ensure!(
            files.contains_key(required),
            "runtime package is missing {required}"
        );
    }
    for payload in [
        "bin/GameInputRedist.dll",
        "bin/GameInputRawInputProxy.exe",
        "bin/inputnext_runtime.dll",
        "bin/xinput1_3.dll",
        "bin/xinput1_4.dll",
        "bin/xinput9_1_0.dll",
    ] {
        validate_pe_machine(&root.join(payload), architecture)?;
    }
    Ok(())
}

fn enumerate_runtime_files(root: &Path) -> Result<BTreeMap<String, String>> {
    let root = dunce::canonicalize(root)?;
    let mut files = BTreeMap::new();
    let mut folded = BTreeSet::new();
    for entry in walkdir::WalkDir::new(&root)
        .follow_links(false)
        .min_depth(1)
    {
        let entry = entry.context("could not enumerate runtime package")?;
        let metadata = fs::symlink_metadata(entry.path())?;
        ensure!(
            !metadata.file_type().is_symlink(),
            "runtime package contains a link"
        );
        if metadata.is_dir() {
            continue;
        }
        ensure!(
            metadata.is_file(),
            "runtime package contains a non-file entry"
        );
        let relative = entry.path().strip_prefix(&root)?;
        let path = normalized_archive_path(relative)?;
        ensure!(
            folded.insert(path.to_ascii_lowercase()),
            "runtime package contains case aliases"
        );
        files.insert(path, hash_file(entry.path())?);
    }
    Ok(files)
}

fn verify_identical_sdk_headers(
    root: &Path,
    core_source: &Path,
    effects_source: &Path,
) -> Result<()> {
    for (name, source) in [
        ("input_next_core.h", core_source),
        ("input_next_effects.h", effects_source),
    ] {
        let runtime = regular_file_metadata(&root.join("sdk").join(name), "runtime SDK header")?;
        ensure!(
            runtime.len() <= MAX_RUNTIME_METADATA_BYTES,
            "runtime SDK header exceeds the size limit"
        );
        let source_bytes = fs::read(source).with_context(|| {
            format!("could not read source-tag SDK header {}", source.display())
        })?;
        ensure!(
            fs::read(root.join("sdk").join(name))? == source_bytes,
            "runtime SDK header differs from its source-tag header"
        );
    }
    Ok(())
}

fn source_sdk_header(root: &Path, name: &str) -> Result<PathBuf> {
    let mut current = root.to_path_buf();
    for component in ["third_party", "inputnext", "include"] {
        current.push(component);
        let metadata = fs::symlink_metadata(&current).with_context(|| {
            format!("source-tag SDK directory {} is missing", current.display())
        })?;
        ensure!(
            metadata.is_dir() && !metadata.file_type().is_symlink(),
            "source-tag SDK directory must not be a link"
        );
    }
    let path = current.join(name);
    let metadata = regular_file_metadata(&path, "source-tag SDK header")?;
    ensure!(
        metadata.len() <= MAX_RUNTIME_METADATA_BYTES,
        "source-tag SDK header exceeds the size limit"
    );
    Ok(path)
}

fn validate_pe_machine(path: &Path, architecture: Architecture) -> Result<()> {
    let metadata = regular_file_metadata(path, "runtime DLL")?;
    ensure!(
        metadata.len() <= MAX_PE_BYTES,
        "runtime DLL exceeds the PE size limit"
    );
    let mut file = File::open(path).context("could not open runtime DLL")?;
    let mut dos = [0_u8; 64];
    file.read_exact(&mut dos)
        .context("runtime DLL has a truncated DOS header")?;
    ensure!(&dos[..2] == b"MZ", "runtime DLL is not a DOS/PE image");
    let pe_offset = u64::from(u32::from_le_bytes(dos[0x3c..0x40].try_into()?));
    ensure!(
        pe_offset >= 64
            && pe_offset
                .checked_add(26)
                .is_some_and(|end| end <= metadata.len()),
        "runtime DLL has an invalid PE header offset"
    );
    file.seek(SeekFrom::Start(pe_offset))
        .context("could not seek to runtime PE header")?;
    let mut coff = [0_u8; 24];
    file.read_exact(&mut coff)
        .context("runtime DLL has a truncated PE/COFF header")?;
    ensure!(&coff[..4] == b"PE\0\0", "runtime DLL has no PE signature");
    let machine = u16::from_le_bytes([coff[4], coff[5]]);
    let optional_size = u16::from_le_bytes([coff[20], coff[21]]);
    ensure!(optional_size >= 2, "runtime PE optional header is missing");
    let expected = match architecture {
        Architecture::X64 => 0x8664,
        Architecture::X86 => 0x014c,
    };
    ensure!(
        machine == expected,
        "runtime PE machine does not match {}",
        architecture.as_str()
    );
    let mut optional_magic = [0_u8; 2];
    file.read_exact(&mut optional_magic)
        .context("runtime PE optional header is truncated")?;
    let expected_magic = match architecture {
        Architecture::X64 => 0x020b,
        Architecture::X86 => 0x010b,
    };
    ensure!(
        u16::from_le_bytes(optional_magic) == expected_magic,
        "runtime PE optional-header format does not match {}",
        architecture.as_str()
    );
    Ok(())
}

fn create_child_directory(parent: &Path, name: &str) -> Result<PathBuf> {
    let path = parent.join(name);
    match fs::create_dir(&path) {
        Ok(()) => {}
        Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
            let metadata = fs::symlink_metadata(&path)
                .with_context(|| format!("could not inspect directory {}", path.display()))?;
            ensure!(
                metadata.is_dir() && !metadata.file_type().is_symlink(),
                "runtime output directory must be a regular directory"
            );
        }
        Err(error) => {
            return Err(error)
                .with_context(|| format!("could not create directory {}", path.display()));
        }
    }
    let canonical = dunce::canonicalize(&path)
        .with_context(|| format!("could not resolve directory {}", path.display()))?;
    ensure!(
        canonical.starts_with(parent),
        "runtime output directory escaped its parent"
    );
    Ok(canonical)
}

fn require_regular_file(path: &Path, label: &str) -> Result<PathBuf> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect {label} {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "{label} must be a regular non-symlink file"
    );
    dunce::canonicalize(path)
        .with_context(|| format!("could not resolve {label} {}", path.display()))
}

fn regular_file_metadata(path: &Path, label: &str) -> Result<fs::Metadata> {
    let metadata = fs::symlink_metadata(path).with_context(|| format!("{label} is missing"))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "{label} must be a regular non-symlink file"
    );
    Ok(metadata)
}

fn normalized_archive_path(path: &Path) -> Result<String> {
    ensure!(
        path.components()
            .all(|component| matches!(component, Component::Normal(_))),
        "runtime package path is not normalized"
    );
    let mut parts = Vec::new();
    for component in path.components() {
        let Component::Normal(component) = component else {
            anyhow::bail!("runtime package path is not normalized");
        };
        let component = component
            .to_str()
            .context("runtime package path is not Unicode")?;
        ensure!(
            !component.is_empty()
                && !component.contains(['/', '\\', ':', '\0'])
                && component != "."
                && component != "..",
            "runtime package path has an unsafe component"
        );
        parts.push(component);
    }
    ensure!(!parts.is_empty(), "runtime package path is empty");
    Ok(parts.join("/"))
}

fn validate_manifest_path(path: &str) -> Result<()> {
    ensure!(
        !path.is_empty()
            && !path.contains(['\\', ':', '\0'])
            && !path.starts_with('/')
            && path.split('/').all(|component| {
                !component.is_empty() && component != "." && component != ".."
            }),
        "runtime package manifest contains a noncanonical path"
    );
    Ok(())
}

#[derive(Clone, Debug)]
struct PreparedArchive {
    input: RuntimeArchiveInput,
    path: PathBuf,
    sha256: String,
    size_bytes: u64,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct RuntimePackageManifest {
    schema: u32,
    files: BTreeMap<String, String>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct RuntimeBuildInfo {
    name: String,
    version: String,
    architecture: String,
    compiler: String,
    configuration: String,
    optimization: String,
    pgo: String,
    gameinput: String,
    tomlplusplus: String,
    source_revision: String,
    source_modified: Option<bool>,
}
