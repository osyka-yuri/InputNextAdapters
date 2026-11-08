//! Shared filesystem, source inventory, and Git revision checks.

use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use crate::process::{CommandOutput, CommandSpec, Runner};
use crate::tooling::hash_file;

use super::types::SourceFile;
use anyhow::{Context, Result, ensure};

const MAX_GIT_OUTPUT_BYTES: usize = 64 * 1024 * 1024;
const GIT_TIMEOUT: Duration = Duration::from_secs(30);
const OPENVR_MACOS_FRAMEWORK: &str = "extern/openvr/bin/osx64/OpenVR.framework";

pub(super) fn copy_dependency_tree(
    dependency: &str,
    source: &Path,
    destination: &Path,
) -> Result<Vec<SourceFile>> {
    let source = dunce::canonicalize(source).context("could not resolve dependency source root")?;
    fs::create_dir_all(destination)
        .context("could not create source package dependency directory")?;
    // CommonLibSSE uses OpenVR's Windows headers and library directly.
    let unused_framework = source.join(OPENVR_MACOS_FRAMEWORK);
    let mut files = Vec::new();
    let mut folded_paths = BTreeSet::new();
    for entry in walkdir::WalkDir::new(&source)
        .follow_links(false)
        .min_depth(1)
        .into_iter()
        .filter_entry(|entry| dependency != "commonlibsse" || entry.path() != unused_framework)
    {
        let entry = entry.context("could not enumerate dependency source tree")?;
        let relative = entry.path().strip_prefix(&source)?;
        if excluded_dependency_path(relative) {
            continue;
        }
        let metadata = fs::symlink_metadata(entry.path())?;
        if assistant_metadata(relative, entry.path(), &metadata)? {
            continue;
        }
        ensure!(
            !metadata.file_type().is_symlink(),
            "dependency source tree contains a link: {}",
            entry.path().display()
        );
        if metadata.is_dir() {
            continue;
        }
        ensure!(
            metadata.is_file(),
            "dependency source tree contains a non-file entry"
        );
        let relative_text = normalized_relative(relative)?;
        ensure!(
            folded_paths.insert(relative_text.to_ascii_lowercase()),
            "dependency source contains case-colliding paths"
        );
        let target = destination.join(relative);
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::copy(entry.path(), &target)
            .with_context(|| format!("could not copy dependency source file {relative_text}"))?;
        files.push(SourceFile {
            path: relative_text,
            size: metadata.len(),
            sha256: hash_file(&target)?,
        });
    }
    files.sort_by(|left, right| left.path.cmp(&right.path));
    ensure!(
        !files.is_empty(),
        "dependency source tree contains no files"
    );
    Ok(files)
}

pub(super) fn inventory_dependency_files(root: &Path) -> Result<Vec<SourceFile>> {
    let root = dunce::canonicalize(root).context("could not resolve dependency tree")?;
    let mut files = Vec::new();
    let mut folded = BTreeSet::new();
    for entry in walkdir::WalkDir::new(&root)
        .follow_links(false)
        .min_depth(1)
    {
        let entry = entry.context("could not enumerate dependency source inventory")?;
        let relative = entry.path().strip_prefix(&root)?;
        if excluded_dependency_path(relative) {
            continue;
        }
        let metadata = fs::symlink_metadata(entry.path())?;
        ensure!(
            !metadata.file_type().is_symlink(),
            "dependency source tree contains a link"
        );
        if metadata.is_dir() {
            continue;
        }
        ensure!(
            metadata.is_file(),
            "dependency source tree contains a non-file entry"
        );
        let path = normalized_relative(relative)?;
        ensure!(
            folded.insert(path.to_ascii_lowercase()),
            "dependency source has case-colliding paths"
        );
        files.push(SourceFile {
            path,
            size: metadata.len(),
            sha256: hash_file(entry.path())?,
        });
    }
    files.sort_by(|left, right| left.path.cmp(&right.path));
    Ok(files)
}

pub(super) fn inventory_dependency_paths(root: &Path) -> Result<Vec<String>> {
    let mut paths = Vec::new();
    for entry in walkdir::WalkDir::new(root).follow_links(false).min_depth(1) {
        let entry = entry.context("could not enumerate packaged dependency files")?;
        let relative = entry.path().strip_prefix(root)?;
        if excluded_dependency_path(relative) {
            continue;
        }
        let metadata = fs::symlink_metadata(entry.path())?;
        ensure!(
            !metadata.file_type().is_symlink(),
            "packaged dependencies contain a link"
        );
        if metadata.is_dir() {
            continue;
        }
        ensure!(
            metadata.is_file(),
            "packaged dependency entry is not a regular file"
        );
        paths.push(normalized_relative(relative)?);
    }
    paths.sort();
    Ok(paths)
}

pub(super) fn safe_bundle_path(root: &Path, relative: &str) -> Result<PathBuf> {
    let path = Path::new(relative);
    ensure!(
        path.components()
            .all(|part| matches!(part, Component::Normal(_)))
            && !relative.contains(['\\', ':', '\0']),
        "source dependency path is not canonical"
    );
    let root = dunce::canonicalize(root).context("could not resolve source dependency root")?;
    let candidate = root.join(path);
    let canonical = dunce::canonicalize(&candidate)
        .with_context(|| format!("source dependency path is missing: {relative}"))?;
    ensure!(
        canonical.starts_with(root),
        "source dependency path escaped its package root"
    );
    let metadata = fs::symlink_metadata(&candidate)?;
    ensure!(
        !metadata.file_type().is_symlink(),
        "source dependency path is a link"
    );
    Ok(canonical)
}

fn excluded_dependency_path(path: &Path) -> bool {
    path.components().any(|component| {
        let Component::Normal(value) = component else {
            return false;
        };
        let value = value.to_string_lossy().to_ascii_lowercase();
        value == ".git"
            || value == "cmakefiles"
            || value == "_deps"
            || value.starts_with("cmake-build-")
    }) || matches!(
        path.file_name().and_then(OsStr::to_str),
        Some("CMakeCache.txt" | "cmake_install.cmake" | "build.ninja" | "compile_commands.json")
    )
}

fn assistant_metadata(relative: &Path, source: &Path, metadata: &fs::Metadata) -> Result<bool> {
    let relative = normalized_relative(relative)?;
    if metadata.file_type().is_symlink() {
        // Only omit the exact aliases present in the pinned CommonLibSSE source.
        let expected_target = match relative.as_str() {
            ".cody.md" | ".cursorrules" | "COPILOT.md" => ".claude/CLAUDE.md",
            ".factory/AGENTS.md" => "../.claude/CLAUDE.md",
            _ => return Ok(false),
        };
        return Ok(fs::read_link(source)? == Path::new(expected_target));
    }
    // Omit upstream assistant metadata while copying. Inventory validation must
    // still see every entry if metadata is later injected into a packaged tree.
    Ok(matches!(
        relative.as_str(),
        ".cody.md"
            | ".cursorrules"
            | "AGENTS.md"
            | "CLAUDE.md"
            | "COPILOT.md"
            | ".claude"
            | ".factory"
    ) || relative.starts_with(".claude/")
        || relative.starts_with(".factory/"))
}

pub(super) fn list_authored_paths(
    runner: &mut dyn Runner,
    git: &Path,
    root: &Path,
) -> Result<Vec<String>> {
    let tracked = git_output(runner, git, root, ["ls-files", "--cached", "-z"])?;
    let untracked = git_output(
        runner,
        git,
        root,
        ["ls-files", "--others", "--exclude-standard", "-z"],
    )?;
    let mut paths = BTreeSet::new();
    for bytes in [&tracked.stdout, &untracked.stdout] {
        for raw in bytes
            .split(|byte| *byte == 0)
            .filter(|path| !path.is_empty())
        {
            let text = std::str::from_utf8(raw).context("authored source path is not UTF-8")?;
            let normalized = normalized_relative(Path::new(text))?;
            ensure!(
                !normalized
                    .split('/')
                    .any(|part| part.eq_ignore_ascii_case(".git")),
                "source checkout file list includes Git metadata"
            );
            paths.insert(normalized);
        }
    }
    ensure!(!paths.is_empty(), "source package has no authored files");
    Ok(paths.into_iter().collect())
}

pub(super) fn normalized_relative(path: &Path) -> Result<String> {
    ensure!(
        path.components()
            .all(|component| matches!(component, Component::Normal(_))),
        "source path is not normalized"
    );
    let mut parts = Vec::new();
    for component in path.components() {
        let Component::Normal(part) = component else {
            anyhow::bail!("source path is not normalized")
        };
        let part = part.to_str().context("source path is not valid Unicode")?;
        ensure!(
            !part.is_empty()
                && !part.contains(['/', '\\', ':', '\0'])
                && part != "."
                && part != "..",
            "source path has an unsafe component"
        );
        parts.push(part);
    }
    ensure!(!parts.is_empty(), "source path is empty");
    Ok(parts.join("/"))
}

pub(super) fn git_revision_matches(
    runner: &mut dyn Runner,
    git: &Path,
    root: &Path,
    expected: &str,
) -> Result<bool> {
    let output = git_output(runner, git, root, ["rev-parse", "HEAD"])?;
    let actual = std::str::from_utf8(&output.stdout)
        .context("Git returned non-UTF-8 submodule revision")?
        .trim();
    Ok(output.code == Some(0) && actual == expected)
}

pub(super) fn find_git() -> Result<PathBuf> {
    let path = env::var_os("PATH").context("PATH is unavailable for source packaging")?;
    for directory in env::split_paths(&path) {
        for name in ["git.exe", "git"] {
            let candidate = directory.join(name);
            if fs::symlink_metadata(&candidate).is_ok_and(|metadata| metadata.is_file()) {
                return dunce::canonicalize(&candidate).with_context(|| {
                    format!("could not resolve Git executable {}", candidate.display())
                });
            }
        }
    }
    anyhow::bail!("Git executable was not found on PATH")
}

fn git_output<const N: usize>(
    runner: &mut dyn Runner,
    git: &Path,
    cwd: &Path,
    args: [&str; N],
) -> Result<CommandOutput> {
    let output = runner.run(&CommandSpec {
        program: git.to_path_buf(),
        args: args.into_iter().map(OsString::from).collect(),
        cwd: cwd.to_path_buf(),
        env: BTreeMap::default(),
        timeout: GIT_TIMEOUT,
        output_limit: MAX_GIT_OUTPUT_BYTES,
    })?;
    ensure!(
        output.code == Some(0),
        "Git command failed while preparing a source package\n{}",
        output.diagnostics()
    );
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::{OPENVR_MACOS_FRAMEWORK, copy_dependency_tree, inventory_dependency_files};
    use anyhow::Result;
    use std::fs;

    #[test]
    fn copies_authored_vendor_build_files_and_excludes_generated_metadata() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let source = temporary.path().join("vendor");
        let destination = temporary.path().join("packaged");
        fs::create_dir_all(source.join("build/CMakeFiles"))?;
        fs::create_dir_all(source.join("_deps/fmt"))?;
        fs::create_dir_all(source.join(".git"))?;
        let authored = [
            ("build/CompilerAndLinker.cmake", "set(COMPILER_FLAGS /W4)\n"),
            ("build/DirectXTK-config.cmake.in", "@PACKAGE_INIT@\n"),
            ("build/DirectXTK.pc.in", "prefix=@prefix@\n"),
        ];
        for (relative, contents) in authored {
            let path = source.join(relative);
            fs::write(path, contents)?;
        }
        for (relative, contents) in [
            ("CMakeCache.txt", "generated cache"),
            ("cmake_install.cmake", "generated install script"),
            ("build.ninja", "generated build file"),
            ("compile_commands.json", "[]"),
            ("build/CMakeCache.txt", "generated nested cache"),
            ("build/CMakeFiles/generated.cmake", "generated target"),
            ("_deps/fmt/generated.h", "generated dependency"),
            (".git/config", "repository metadata"),
            (".cody.md", ".claude/CLAUDE.md"),
            (".cursorrules", ".claude/CLAUDE.md"),
            ("AGENTS.md", "assistant metadata"),
            ("COPILOT.md", ".claude/CLAUDE.md"),
        ] {
            fs::write(source.join(relative), contents)?;
        }

        let copied = copy_dependency_tree("directxtk", &source, &destination)?;
        let inventory = inventory_dependency_files(&destination)?;
        assert_eq!(copied, inventory);
        assert_eq!(copied.len(), authored.len());
        for (relative, contents) in authored {
            assert_eq!(fs::read_to_string(destination.join(relative))?, contents);
        }
        Ok(())
    }

    #[test]
    fn metadata_copy_exclusions_do_not_hide_packaged_inventory_entries() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let source = temporary.path().join("vendor");
        let destination = temporary.path().join("packaged");
        fs::create_dir_all(source.join("src"))?;
        fs::write(source.join("AGENTS.md"), "assistant metadata")?;
        fs::write(source.join("src/AGENTS.md"), "authored nested file")?;
        let copied = copy_dependency_tree("commonlibsse", &source, &destination)?;
        assert_eq!(copied.len(), 1);
        assert_eq!(copied[0].path, "src/AGENTS.md");
        fs::write(destination.join("AGENTS.md"), "injected metadata")?;
        assert_ne!(copied, inventory_dependency_files(&destination)?);
        Ok(())
    }

    #[test]
    fn omits_only_the_unused_openvr_framework_from_commonlibsse() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let source = temporary.path().join("vendor");
        let framework = source.join(OPENVR_MACOS_FRAMEWORK);
        fs::create_dir_all(framework.join("Versions/A"))?;
        fs::write(framework.join("Versions/A/OpenVR"), "macOS binary")?;
        #[cfg(unix)]
        std::os::unix::fs::symlink("Versions/A/OpenVR", framework.join("OpenVR"))?;
        let windows_files = [
            "extern/openvr/headers/openvr.h",
            "extern/openvr/lib/win64/openvr_api.lib",
        ];
        fs::create_dir_all(source.join("extern/openvr/headers"))?;
        fs::create_dir_all(source.join("extern/openvr/lib/win64"))?;
        for relative in windows_files {
            fs::write(source.join(relative), relative)?;
        }
        let destination = temporary.path().join("packaged");
        let copied = copy_dependency_tree("commonlibsse", &source, &destination)?;
        assert_eq!(copied, inventory_dependency_files(&destination)?);
        assert_eq!(
            copied
                .iter()
                .map(|file| file.path.as_str())
                .collect::<Vec<_>>(),
            windows_files
        );

        let injected = destination.join(OPENVR_MACOS_FRAMEWORK);
        fs::create_dir_all(&injected)?;
        fs::write(injected.join("OpenVR"), "unexpected package entry")?;
        assert_ne!(copied, inventory_dependency_files(&destination)?);

        let other = temporary.path().join("other");
        #[cfg(unix)]
        assert!(copy_dependency_tree("other", &source, &other).is_err());
        #[cfg(not(unix))]
        assert_eq!(copy_dependency_tree("other", &source, &other)?.len(), 3);
        Ok(())
    }

    #[cfg(unix)]
    #[test]
    fn skips_assistant_metadata_links_but_rejects_source_links() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let source = temporary.path().join("vendor");
        let destination = temporary.path().join("packaged");
        fs::create_dir_all(source.join(".claude"))?;
        fs::create_dir_all(source.join(".factory"))?;
        fs::write(source.join("header.h"), "authored header")?;
        let authored = inventory_dependency_files(&source)?;
        assert_eq!(authored.len(), 1);
        assert_eq!(authored[0].path, "header.h");

        fs::write(source.join(".claude/CLAUDE.md"), "assistant metadata")?;
        for path in [".cody.md", ".cursorrules", "COPILOT.md"] {
            std::os::unix::fs::symlink(".claude/CLAUDE.md", source.join(path))?;
        }
        std::os::unix::fs::symlink("../.claude/CLAUDE.md", source.join(".factory/AGENTS.md"))?;
        let copied = copy_dependency_tree("commonlibsse", &source, &destination)?;
        assert_eq!(copied, authored);
        assert_eq!(copied, inventory_dependency_files(&destination)?);
        let Err(error) = inventory_dependency_files(&source) else {
            anyhow::bail!("inventory accepted the unbundled links");
        };
        assert_eq!(error.to_string(), "dependency source tree contains a link");

        std::os::unix::fs::symlink("header.h", source.join("linked_header.h"))?;
        assert!(
            copy_dependency_tree("commonlibsse", &source, &temporary.path().join("rejected"))
                .is_err()
        );
        fs::remove_file(source.join("linked_header.h"))?;
        fs::remove_file(source.join(".cody.md"))?;
        std::os::unix::fs::symlink("header.h", source.join(".cody.md"))?;
        assert!(
            copy_dependency_tree(
                "commonlibsse",
                &source,
                &temporary.path().join("wrong-alias")
            )
            .is_err()
        );
        fs::remove_file(source.join(".cody.md"))?;
        std::os::unix::fs::symlink(".claude/CLAUDE.md", source.join("AGENTS.md"))?;
        assert!(
            copy_dependency_tree(
                "commonlibsse",
                &source,
                &temporary.path().join("unlisted-alias")
            )
            .is_err()
        );
        Ok(())
    }
}
