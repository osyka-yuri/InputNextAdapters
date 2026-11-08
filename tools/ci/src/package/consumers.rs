//! Relocated installed and extracted-source consumer checks for produced packages.

use std::collections::BTreeMap;
use std::fs;
use std::path::Path;
use std::time::Duration;

use anyhow::{Context, Result, ensure};

use super::{ConsumerEvidence, PackageExpectation, archive};
use crate::model::{Architecture, ExecutionContext, Product};
use crate::process::{CommandSpec, Runner, run_checked};
use crate::tooling::{ArchiveFormat, command_environment, extract_archive, inspect_archive};

const CONSUMER_TIMEOUT: Duration = Duration::from_mins(45);
const CONSUMER_OUTPUT_LIMIT: usize = 32 * 1024 * 1024;
const SKSE_ARCHIVE_SHA256: &str =
    "1825024212a20a6a197bbcd308073b497e516139ad48d60685d5b18ab4bbe76d";

#[derive(Clone, Copy, Debug)]
pub(super) struct SourceConsumerInputs<'a> {
    pub(super) context: &'a ExecutionContext,
    pub(super) architecture: Architecture,
    pub(super) native_build_directory: &'a Path,
    pub(super) work_root: &'a Path,
}

pub(super) fn verify_foundation_installed_package(
    prefix: &Path,
    binary_sha256: &str,
    context: &ExecutionContext,
    architecture: Architecture,
    work_root: &Path,
    runner: &mut dyn Runner,
) -> Result<ConsumerEvidence> {
    fs::create_dir_all(work_root).context("could not create installed consumer work root")?;
    let relocated = work_root.join("foundation-relocated-install");
    ensure!(
        !relocated.exists(),
        "foundation relocated consumer directory already exists"
    );
    copy_directory(prefix, &relocated)?;
    ensure!(
        fs::read_dir(&relocated)?.next().is_some(),
        "foundation installed package relocation produced no files"
    );
    let source = work_root.join("foundation-installed-consumer-source");
    fs::create_dir(&source).context("could not create installed consumer source")?;
    fs::write(
        source.join("CMakeLists.txt"),
        "cmake_minimum_required(VERSION 4.4.4)\nproject(InputNextInstalledConsumer LANGUAGES CXX)\nfind_package(InputNextAdapters CONFIG REQUIRED)\nadd_executable(inputnext_consumer main.cpp)\ntarget_link_libraries(inputnext_consumer PRIVATE InputNextAdapters::runtime_client InputNextAdapters::effects)\n",
    )?;
    fs::write(
        source.join("main.cpp"),
        "#include <input_next_core.h>\n#include <input_next_effects.h>\n#include <inputnext_adapters/runtime_client.hpp>\nint main() { inputnext::adapters::RuntimeClient client; return client.connected() ? 1 : 0; }\n",
    )?;
    let build = work_root.join("foundation-installed-consumer-build");
    configure(
        context,
        runner,
        &source,
        &build,
        &[
            format!("-DCMAKE_PREFIX_PATH:PATH={}", relocated.display()),
            "-DCMAKE_CXX_COMPILER:FILEPATH=cl".to_owned(),
            "-DCMAKE_BUILD_TYPE:STRING=Release".to_owned(),
            format!(
                "-DINPUTNEXTADAPTERS_EXPECTED_BITS:STRING={}",
                architecture_bits(architecture)
            ),
        ],
    )?;
    build_targets(context, runner, &build, &["inputnext_consumer"])?;
    let executable = build.join(if cfg!(windows) {
        "inputnext_consumer.exe"
    } else {
        "inputnext_consumer"
    });
    run_checked(
        runner,
        &CommandSpec {
            program: dunce::canonicalize(&executable)
                .context("installed consumer executable was not built")?,
            args: Vec::new(),
            cwd: dunce::canonicalize(&relocated)?,
            env: command_environment(&context.toolset)?,
            timeout: CONSUMER_TIMEOUT,
            output_limit: CONSUMER_OUTPUT_LIMIT,
        },
    )?;
    Ok(ConsumerEvidence {
        kind: "installed-relocated-consumer".to_owned(),
        archive_sha256: binary_sha256.to_owned(),
        configured: true,
        built_targets: vec!["inputnext_consumer".to_owned()],
        diagnostic: None,
    })
}

pub(super) fn verify_source_package_consumer(
    source_archive: &Path,
    expectation: &PackageExpectation,
    source_sha256: &str,
    inputs: SourceConsumerInputs<'_>,
    runner: &mut dyn Runner,
) -> Result<ConsumerEvidence> {
    let SourceConsumerInputs {
        context,
        architecture,
        native_build_directory,
        work_root,
    } = inputs;
    fs::create_dir_all(work_root).context("could not create source consumer work root")?;
    let extracted = work_root.join(format!("{}-source-consumer", expectation.product.as_str()));
    ensure!(
        !extracted.exists(),
        "source consumer extraction directory already exists"
    );
    inspect_archive(source_archive, ArchiveFormat::Zip)?;
    extract_archive(source_archive, ArchiveFormat::Zip, &extracted)?;
    archive::inspect_extracted_package(&extracted, expectation)?;
    ensure_no_git_metadata(&extracted)?;

    let build = work_root.join(format!(
        "{}-source-consumer-build",
        expectation.product.as_str()
    ));
    let configure_arguments = match expectation.product {
        Product::Foundation => {
            vec![
                "-DINPUTNEXTADAPTERS_BUILD_TESTS:BOOL=OFF".to_owned(),
                "-DINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON".to_owned(),
                "-DCMAKE_BUILD_TYPE:STRING=Release".to_owned(),
                format!(
                    "-DINPUTNEXTADAPTERS_EXPECTED_BITS:STRING={}",
                    architecture_bits(architecture)
                ),
            ]
        }
        Product::SkyrimSe => adapter_source_arguments(
            "INPUTNEXT_SKYRIM_BUILD_PLUGIN:BOOL=ON",
            "INPUTNEXT_SKYRIM_BUILD_TESTS:BOOL=OFF",
        ),
        Product::Fallout4 => adapter_source_arguments(
            "INPUTNEXT_FALLOUT4_BUILD_PLUGIN:BOOL=ON",
            "INPUTNEXT_FALLOUT4_BUILD_TESTS:BOOL=OFF",
        ),
        Product::SkyrimLe => adapter_source_arguments(
            "INPUTNEXT_SKYRIM_LE_BUILD_PLUGIN:BOOL=ON",
            "INPUTNEXT_SKYRIM_LE_BUILD_TESTS:BOOL=OFF",
        ),
    };
    let source = match expectation.product {
        Product::Foundation => extracted.clone(),
        Product::SkyrimSe => extracted.join("adapters/skyrim_se"),
        Product::SkyrimLe => extracted.join("adapters/skyrim_le"),
        Product::Fallout4 => extracted.join("adapters/fallout4"),
    };
    if matches!(expectation.product, Product::SkyrimSe | Product::Fallout4) {
        let manifest = dunce::canonicalize(extracted.join("inputnext-adapters-source.json"))?;
        let mut args = configure_arguments;
        args.extend([
            format!(
                "-DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH={}",
                manifest.display()
            ),
            "-DFETCHCONTENT_FULLY_DISCONNECTED:BOOL=ON".to_owned(),
        ]);
        configure(context, runner, &source, &build, &args)?;
        verify_declared_fetchcontent_overrides(&build, &extracted, expectation.product)?;
    } else if expectation.product == Product::Foundation {
        configure(context, runner, &source, &build, &configure_arguments)?;
    } else {
        let mut args = configure_arguments;
        if expectation.product == Product::SkyrimLe
            && let Some(archive) = find_verified_skse_archive(native_build_directory)?
        {
            args.push(format!(
                "-DINPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE:FILEPATH={}",
                archive.display()
            ));
        }
        configure(context, runner, &source, &build, &args)?;
    }
    let target = expectation.product.source_consumer_target();
    build_targets(context, runner, &build, &[target])?;
    Ok(ConsumerEvidence {
        kind: "extracted-source-consumer".to_owned(),
        archive_sha256: source_sha256.to_owned(),
        configured: true,
        built_targets: vec![target.to_owned()],
        diagnostic: None,
    })
}

fn adapter_source_arguments(plugin: &str, tests: &str) -> Vec<String> {
    vec![
        format!("-D{plugin}"),
        format!("-D{tests}"),
        "-DCMAKE_CXX_COMPILER:FILEPATH=cl".to_owned(),
        "-DCMAKE_BUILD_TYPE:STRING=Release".to_owned(),
    ]
}

fn configure(
    context: &ExecutionContext,
    runner: &mut dyn Runner,
    source: &Path,
    build: &Path,
    arguments: &[String],
) -> Result<()> {
    ensure!(!build.exists(), "consumer build directory already exists");
    let cmake = context.toolset.executable("cmake")?;
    let mut args = vec![
        "-S".into(),
        source.as_os_str().to_owned(),
        "-B".into(),
        build.as_os_str().to_owned(),
        "-G".into(),
        "Ninja".into(),
    ];
    args.extend(arguments.iter().cloned().map(Into::into));
    run_checked(
        runner,
        &CommandSpec {
            program: cmake.to_path_buf(),
            args,
            cwd: context.repository.clone(),
            env: command_environment(&context.toolset)?,
            timeout: CONSUMER_TIMEOUT,
            output_limit: CONSUMER_OUTPUT_LIMIT,
        },
    )?;
    Ok(())
}

fn build_targets(
    context: &ExecutionContext,
    runner: &mut dyn Runner,
    build: &Path,
    targets: &[&str],
) -> Result<()> {
    let cmake = context.toolset.executable("cmake")?;
    let mut args = vec![
        "--build".into(),
        build.as_os_str().to_owned(),
        "--parallel".into(),
        context.jobs.to_string().into(),
    ];
    for target in targets {
        args.push("--target".into());
        args.push((*target).into());
    }
    run_checked(
        runner,
        &CommandSpec {
            program: cmake.to_path_buf(),
            args,
            cwd: context.repository.clone(),
            env: command_environment(&context.toolset)?,
            timeout: CONSUMER_TIMEOUT,
            output_limit: CONSUMER_OUTPUT_LIMIT,
        },
    )?;
    Ok(())
}

fn ensure_no_git_metadata(root: &Path) -> Result<()> {
    for entry in walkdir::WalkDir::new(root).follow_links(false) {
        let entry = entry.context("could not inspect extracted source package")?;
        ensure!(
            !entry
                .file_name()
                .to_string_lossy()
                .eq_ignore_ascii_case(".git"),
            "source package must configure without Git metadata"
        );
    }
    Ok(())
}

fn verify_declared_fetchcontent_overrides(
    build: &Path,
    source_root: &Path,
    product: Product,
) -> Result<()> {
    let cache = fs::read_to_string(build.join("CMakeCache.txt"))
        .context("source consumer did not produce a CMake cache")?;
    let expected = match product {
        Product::SkyrimSe => BTreeMap::from([
            (
                "FETCHCONTENT_SOURCE_DIR_COMMONLIBSSE",
                "dependencies/skyrim-se/commonlibsse",
            ),
            (
                "FETCHCONTENT_SOURCE_DIR_DIRECTXTK",
                "dependencies/skyrim-se/directxtk",
            ),
            ("FETCHCONTENT_SOURCE_DIR_FMT", "dependencies/skyrim-se/fmt"),
            (
                "FETCHCONTENT_SOURCE_DIR_RAPIDCSV",
                "dependencies/skyrim-se/rapidcsv",
            ),
            (
                "FETCHCONTENT_SOURCE_DIR_SPDLOG",
                "dependencies/skyrim-se/spdlog",
            ),
        ]),
        Product::Fallout4 => BTreeMap::from([
            (
                "FETCHCONTENT_SOURCE_DIR_FALLOUT4_COMMONLIBF4",
                "dependencies/fallout4/commonlibf4",
            ),
            (
                "FETCHCONTENT_SOURCE_DIR_SPDLOG",
                "dependencies/fallout4/spdlog",
            ),
        ]),
        _ => anyhow::bail!("source override verification applies only to Skyrim SE and Fallout 4"),
    };
    let mut actual = BTreeMap::new();
    for line in cache
        .lines()
        .filter(|line| line.starts_with("FETCHCONTENT_SOURCE_DIR_"))
    {
        let Some((key, typed)) = line.split_once(':') else {
            continue;
        };
        let Some((_, value)) = typed.split_once('=') else {
            continue;
        };
        if !value.is_empty() {
            actual.insert(key, value);
        }
    }
    ensure!(
        actual.len() == expected.len(),
        "source consumer cache contains undeclared dependency overrides"
    );
    for (key, relative) in expected {
        let expected_path = dunce::canonicalize(source_root.join(relative))?;
        let actual_path = actual
            .get(key)
            .with_context(|| format!("source consumer cache is missing {key}"))?;
        ensure!(
            dunce::canonicalize(actual_path)? == expected_path,
            "source consumer override {key} does not point into the verified source package"
        );
    }
    Ok(())
}

fn copy_directory(source: &Path, destination: &Path) -> Result<()> {
    fs::create_dir_all(destination)?;
    for entry in walkdir::WalkDir::new(source)
        .follow_links(false)
        .min_depth(1)
    {
        let entry = entry.context("could not enumerate installed package")?;
        let relative = entry.path().strip_prefix(source)?;
        let target = destination.join(relative);
        let metadata = fs::symlink_metadata(entry.path())?;
        ensure!(
            !metadata.file_type().is_symlink(),
            "installed package contains a link"
        );
        if metadata.is_dir() {
            fs::create_dir_all(&target)?;
        } else {
            ensure!(
                metadata.is_file(),
                "installed package contains a non-file entry"
            );
            if let Some(parent) = target.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::copy(entry.path(), &target)?;
        }
    }
    Ok(())
}

fn architecture_bits(architecture: Architecture) -> u8 {
    match architecture {
        Architecture::X64 => 64,
        Architecture::X86 => 32,
    }
}

fn find_verified_skse_archive(build_directory: &Path) -> Result<Option<std::path::PathBuf>> {
    let candidate = build_directory.join("_skyrim_le_skse_1_07_03_private/skse_1_07_03.7z");
    let metadata = match fs::symlink_metadata(&candidate) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error).context("could not inspect cached official SKSE archive"),
    };
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "cached official SKSE archive must be a regular file"
    );
    let archive = dunce::canonicalize(&candidate)?;
    ensure!(
        crate::tooling::hash_file(&archive)? == SKSE_ARCHIVE_SHA256,
        "cached official SKSE archive failed the pinned SHA-256 check"
    );
    Ok(Some(archive))
}
