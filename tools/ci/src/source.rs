//! Repository source identity and product version parsing.

use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::ffi::OsString;
use std::fs::{self, File};
use std::io::Read;
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};
use regex::Regex;
use semver::Version;
use sha2::{Digest, Sha256};

use crate::model::{Product, SourceIdentity};
use crate::process::{CommandOutput, CommandSpec, Runner};

const GIT_TIMEOUT: Duration = Duration::from_secs(30);
const GIT_OUTPUT_LIMIT: usize = 64 * 1024 * 1024;
const MAX_PROJECT_FILE_BYTES: u64 = 1024 * 1024;

/// Resolves a product's `CMake` project version as stable semantic versioning.
///
/// # Errors
///
/// Returns an error if the project declaration is missing, unexpected, dynamic, or unstable.
pub fn parse_project_version(product: Product, cmake: &str) -> Result<Version> {
    let pattern =
        Regex::new(r"(?im)^\s*project\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+VERSION\s+([^\s\)]+)")
            .context("static CMake project version pattern is invalid")?;
    let uncommented = strip_cmake_comments(cmake)?;
    let mut declarations = pattern.captures_iter(&uncommented);
    let captures = declarations
        .next()
        .context("CMake project declaration with an explicit VERSION was not found")?;
    ensure!(
        declarations.next().is_none(),
        "CMake project file has more than one project declaration"
    );
    let project_name = captures
        .get(1)
        .context("CMake project name is missing")?
        .as_str();
    ensure!(
        project_name == product_project_name(product),
        "CMake project name '{project_name}' does not match product '{}'",
        product.as_str()
    );
    let version_text = captures
        .get(2)
        .context("CMake project version is missing")?
        .as_str();
    let version = Version::parse(version_text).with_context(|| {
        format!(
            "product '{}' version is not semantic versioning",
            product.as_str()
        )
    })?;
    ensure!(
        version.pre.is_empty() && version.build.is_empty(),
        "product '{}' version must be a stable semantic version",
        product.as_str()
    );
    Ok(version)
}

fn strip_cmake_comments(source: &str) -> Result<String> {
    let bytes = source.as_bytes();
    let mut output = Vec::with_capacity(bytes.len());
    let mut index = 0;
    let mut quoted = false;
    while index < bytes.len() {
        match bytes[index] {
            b'\\' if quoted && index + 1 < bytes.len() => {
                output.extend_from_slice(&bytes[index..index + 2]);
                index += 2;
            }
            b'"' => {
                quoted = !quoted;
                output.push(bytes[index]);
                index += 1;
            }
            b'#' if !quoted => {
                if let Some((after_open, closing)) = bracket_comment_open(bytes, index) {
                    index = skip_bracket_comment(bytes, after_open, &closing, &mut output);
                } else {
                    while index < bytes.len() && bytes[index] != b'\n' {
                        index += 1;
                    }
                }
            }
            byte => {
                output.push(byte);
                index += 1;
            }
        }
    }
    String::from_utf8(output).context("CMake comment removal changed source encoding")
}

fn bracket_comment_open(bytes: &[u8], comment_start: usize) -> Option<(usize, Vec<u8>)> {
    if bytes.get(comment_start + 1) != Some(&b'[') {
        return None;
    }
    let mut open_end = comment_start + 2;
    while bytes.get(open_end) == Some(&b'=') {
        open_end += 1;
    }
    if bytes.get(open_end) != Some(&b'[') {
        return None;
    }
    let mut closing = vec![b']'];
    closing.extend(std::iter::repeat_n(b'=', open_end - comment_start - 2));
    closing.push(b']');
    Some((open_end + 1, closing))
}

fn skip_bracket_comment(
    bytes: &[u8],
    mut index: usize,
    closing: &[u8],
    output: &mut Vec<u8>,
) -> usize {
    while index < bytes.len() {
        if bytes[index..].starts_with(closing) {
            return index + closing.len();
        }
        if bytes[index] == b'\n' {
            output.push(b'\n');
        }
        index += 1;
    }
    index
}

/// Reads and resolves a product's authoritative `CMake` project version.
///
/// # Errors
///
/// Returns an error if the project file is missing or its identity/version is invalid.
pub fn project_version(repository: &Path, product: Product) -> Result<Version> {
    let path = product.source_directory(repository).join("CMakeLists.txt");
    let metadata = fs::symlink_metadata(&path)
        .with_context(|| format!("could not inspect {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "product CMakeLists.txt must be a regular file"
    );
    ensure!(
        metadata.len() <= MAX_PROJECT_FILE_BYTES,
        "product CMakeLists.txt exceeds the size limit"
    );
    let source =
        fs::read_to_string(&path).with_context(|| format!("could not read {}", path.display()))?;
    parse_project_version(product, &source)
}

/// Identifies the current tracked and non-ignored untracked authored source snapshot.
///
/// The snapshot is hashed for dirty or unborn repositories. Ignored build outputs are omitted
/// by Git's `--exclude-standard` handling; tracked source is read from the current worktree.
///
/// # Errors
///
/// Returns an error if Git cannot inspect the repository or an authored file is unsafe to read.
pub fn identify_source(repository: &Path, runner: &mut dyn Runner) -> Result<SourceIdentity> {
    let repository = dunce::canonicalize(repository)
        .with_context(|| format!("could not resolve repository {}", repository.display()))?;
    ensure!(repository.is_dir(), "source repository is not a directory");
    let git = find_git_executable()?;
    let head_output = run_git(runner, &git, &repository, ["rev-parse", "--verify", "HEAD"])?;
    let commit = if head_output.code == Some(0) {
        let value = trim_ascii(&head_output.stdout)?;
        ensure!(
            value.len() == 40 || value.len() == 64,
            "Git returned an invalid source commit identifier"
        );
        ensure!(
            value.bytes().all(|byte| byte.is_ascii_hexdigit()),
            "Git returned a non-hexadecimal source commit identifier"
        );
        Some(value.to_ascii_lowercase())
    } else {
        None
    };

    let status_output = run_git(
        runner,
        &git,
        &repository,
        [
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
            "--no-renames",
        ],
    )?;
    ensure!(
        status_output.code == Some(0),
        "Git could not inspect source worktree status\n{}",
        output_diagnostics(&status_output)
    );
    let modified = commit.is_none() || !status_output.stdout.is_empty();
    if !modified {
        return Ok(SourceIdentity {
            commit,
            modified: false,
            snapshot_sha256: None,
        });
    }

    let tracked = run_git(runner, &git, &repository, ["ls-files", "--cached", "-z"])?;
    ensure!(
        tracked.code == Some(0),
        "Git could not list tracked source files\n{}",
        output_diagnostics(&tracked)
    );
    let untracked = run_git(
        runner,
        &git,
        &repository,
        ["ls-files", "--others", "--exclude-standard", "-z"],
    )?;
    ensure!(
        untracked.code == Some(0),
        "Git could not list non-ignored untracked source files\n{}",
        output_diagnostics(&untracked)
    );

    let mut paths = parse_nul_paths(&tracked.stdout)?;
    paths.extend(parse_nul_paths(&untracked.stdout)?);
    let paths = paths.into_iter().collect::<BTreeSet<_>>();
    let snapshot_sha256 = hash_source_files(&repository, &paths)?;
    Ok(SourceIdentity {
        commit,
        modified: true,
        snapshot_sha256: Some(snapshot_sha256),
    })
}

fn product_project_name(product: Product) -> &'static str {
    match product {
        Product::Foundation => "InputNextAdapters",
        Product::SkyrimSe => "InputNextSkyrim",
        Product::SkyrimLe => "InputNextSkyrimLE",
        Product::Fallout4 => "InputNextFallout4",
    }
}

fn find_git_executable() -> Result<PathBuf> {
    let search_path = env::var_os("PATH").context("PATH is unavailable for source inspection")?;
    let names = if cfg!(windows) {
        ["git.exe", "git"]
    } else {
        ["git", "git"]
    };
    for directory in env::split_paths(&search_path) {
        for name in names {
            let candidate = directory.join(name);
            if fs::symlink_metadata(&candidate).is_ok_and(|metadata| metadata.is_file()) {
                return dunce::canonicalize(&candidate).with_context(|| {
                    format!("could not resolve Git executable {}", candidate.display())
                });
            }
        }
    }
    bail!("Git executable was not found on PATH")
}

fn run_git<const N: usize>(
    runner: &mut dyn Runner,
    git: &Path,
    repository: &Path,
    args: [&str; N],
) -> Result<CommandOutput> {
    runner.run(&CommandSpec {
        program: git.to_path_buf(),
        args: args.into_iter().map(OsString::from).collect(),
        cwd: repository.to_path_buf(),
        env: BTreeMap::default(),
        timeout: GIT_TIMEOUT,
        output_limit: GIT_OUTPUT_LIMIT,
    })
}

fn trim_ascii(bytes: &[u8]) -> Result<String> {
    let value = std::str::from_utf8(bytes).context("Git returned a non-UTF-8 source identity")?;
    let value = value.trim();
    ensure!(!value.is_empty(), "Git returned an empty source identity");
    Ok(value.to_owned())
}

fn parse_nul_paths(bytes: &[u8]) -> Result<Vec<String>> {
    let mut paths = Vec::new();
    for raw_path in bytes.split(|byte| *byte == 0) {
        if raw_path.is_empty() {
            continue;
        }
        let path = std::str::from_utf8(raw_path).context("source file path is not valid UTF-8")?;
        ensure!(
            !path.contains([':', '\\'])
                && path.split('/').all(|part| !matches!(part, "" | "." | "..")),
            "source file path is not a portable normalized path"
        );
        let parsed = Path::new(&path);
        ensure!(!parsed.is_absolute(), "source file path is absolute");
        ensure!(
            parsed
                .components()
                .all(|component| matches!(component, Component::Normal(_))),
            "source file path contains an unsafe component"
        );
        paths.push(path.to_owned());
    }
    Ok(paths)
}

fn hash_source_files(repository: &Path, paths: &BTreeSet<String>) -> Result<String> {
    let mut hasher = Sha256::new();
    for relative in paths {
        let path = repository.join(relative);
        ensure!(
            path.starts_with(repository),
            "source file path escaped the repository"
        );
        let metadata = fs::symlink_metadata(&path)
            .with_context(|| format!("could not inspect authored source {}", path.display()))?;
        let relative_bytes = relative.as_bytes();
        hasher.update(u64::try_from(relative_bytes.len())?.to_be_bytes());
        hasher.update(relative_bytes);
        if metadata.file_type().is_symlink() {
            let target = fs::read_link(&path)
                .with_context(|| format!("could not read authored symlink {}", path.display()))?;
            let target_bytes = target
                .to_str()
                .context("authored symlink target is not valid Unicode")?;
            hasher.update([1_u8]);
            hasher.update(u64::try_from(target_bytes.len())?.to_be_bytes());
            hasher.update(target_bytes.as_bytes());
        } else {
            ensure!(
                metadata.is_file(),
                "authored source entry is not a regular file"
            );
            hasher.update([0_u8]);
            hasher.update(metadata.len().to_be_bytes());
            let mut file = File::open(&path)
                .with_context(|| format!("could not open authored source {}", path.display()))?;
            let mut buffer = vec![0_u8; 64 * 1024];
            loop {
                let count = file.read(&mut buffer).with_context(|| {
                    format!("could not read authored source {}", path.display())
                })?;
                if count == 0 {
                    break;
                }
                hasher.update(&buffer[..count]);
            }
        }
    }
    Ok(encode_sha256(hasher.finalize().as_slice()))
}

fn encode_sha256(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut encoded = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        encoded.push(char::from(HEX[usize::from(byte >> 4)]));
        encoded.push(char::from(HEX[usize::from(byte & 0x0f)]));
    }
    encoded
}

fn output_diagnostics(output: &CommandOutput) -> String {
    format!(
        "stdout:\n{}\nstderr:\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    )
}

#[cfg(test)]
#[path = "source/tests.rs"]
mod tests;
