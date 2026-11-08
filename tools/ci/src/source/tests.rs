use super::{hash_source_files, parse_nul_paths, parse_project_version, product_project_name};
use crate::model::Product;
use crate::process::SystemRunner;
use anyhow::{Context, Result};
use std::collections::BTreeSet;
use std::fs;
use std::path::Path;
use std::process::Command;

#[test]
fn product_project_names_map_to_stable_versions() -> Result<()> {
    for product in [
        Product::Foundation,
        Product::SkyrimSe,
        Product::SkyrimLe,
        Product::Fallout4,
    ] {
        let source = format!(
            "project({} VERSION 1.2.3 LANGUAGES CXX)",
            product_project_name(product)
        );
        assert_eq!(
            parse_project_version(product, &source)?.to_string(),
            "1.2.3"
        );
    }
    Ok(())
}

#[test]
fn project_version_rejects_wrong_identity_and_unstable_semver() {
    assert!(parse_project_version(Product::Foundation, "project(Other VERSION 1.2.3)").is_err());
    assert!(
        parse_project_version(
            Product::Foundation,
            "project(InputNextAdapters VERSION 1.2.3-rc.1)"
        )
        .is_err()
    );
    assert!(
        parse_project_version(
            Product::Foundation,
            "project(InputNextAdapters VERSION ${VERSION})"
        )
        .is_err()
    );
}

#[test]
fn project_version_ignores_line_and_bracket_comments_and_rejects_duplicates() -> Result<()> {
    let source = "# project(Wrong VERSION 9.9.9)\n#[=[\nproject(Wrong VERSION 8.8.8)\n]=]\nproject(InputNextAdapters VERSION 1.2.3 LANGUAGES CXX)\n";
    assert_eq!(
        parse_project_version(Product::Foundation, source)?.to_string(),
        "1.2.3"
    );
    assert!(
        parse_project_version(
            Product::Foundation,
            "project(InputNextAdapters VERSION 1.2.3)\nproject(InputNextAdapters VERSION 1.2.4)"
        )
        .is_err()
    );
    Ok(())
}

#[test]
fn nul_path_parser_rejects_traversal_and_absolute_paths() {
    for path in [
        "../outside",
        "/outside",
        "C:/outside",
        "C:outside",
        "src\\alias.cpp",
        "src/./alias.cpp",
        "src//alias.cpp",
    ] {
        assert!(parse_nul_paths(path.as_bytes()).is_err(), "accepted {path}");
    }
    assert_eq!(
        parse_nul_paths(b"src/file.cpp\0").ok(),
        Some(vec!["src/file.cpp".to_owned()])
    );
}

#[test]
fn source_digest_tracks_path_and_bytes() -> Result<()> {
    let root = tempfile::tempdir()?;
    fs::write(root.path().join("source.cpp"), b"first")?;
    let paths = BTreeSet::from(["source.cpp".to_owned()]);
    let first = hash_source_files(root.path(), &paths)?;
    fs::write(root.path().join("source.cpp"), b"other")?;
    let second = hash_source_files(root.path(), &paths)?;
    assert_ne!(first, second);
    Ok(())
}

#[test]
fn source_identity_supports_unborn_and_dirty_repositories() -> Result<()> {
    let temporary = tempfile::tempdir()?;
    let repository = temporary.path();
    git(repository, &["init", "--quiet"])?;
    let mut runner = SystemRunner;

    let empty = super::identify_source(repository, &mut runner)?;
    assert!(empty.commit.is_none());
    assert!(empty.modified);
    assert_eq!(empty.snapshot_sha256.as_deref().map(str::len), Some(64));

    fs::write(repository.join("authored.txt"), "unborn snapshot\n")?;
    let unborn = super::identify_source(repository, &mut runner)?;
    assert!(unborn.commit.is_none());
    assert!(unborn.modified);
    let unborn_hash = unborn
        .snapshot_sha256
        .as_deref()
        .context("unborn repository did not receive a snapshot hash")?;
    assert_eq!(unborn_hash.len(), 64);

    git(repository, &["add", "authored.txt"])?;
    git(
        repository,
        &[
            "-c",
            "user.name=CI Test",
            "-c",
            "user.email=ci@example.invalid",
            "commit",
            "--quiet",
            "-m",
            "snapshot",
        ],
    )?;
    let clean = super::identify_source(repository, &mut runner)?;
    assert!(clean.commit.is_some());
    assert!(!clean.modified);
    assert!(clean.snapshot_sha256.is_none());

    fs::write(repository.join("authored.txt"), "dirty snapshot\n")?;
    fs::write(
        repository.join("additional.txt"),
        "untracked authored file\n",
    )?;
    let dirty = super::identify_source(repository, &mut runner)?;
    assert!(dirty.modified);
    assert_ne!(dirty.snapshot_sha256.as_deref(), Some(unborn_hash));
    Ok(())
}

fn git(directory: &Path, arguments: &[&str]) -> Result<()> {
    let output = Command::new("git")
        .args(arguments)
        .current_dir(directory)
        .output()?;
    anyhow::ensure!(
        output.status.success(),
        "git fixture command failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(())
}
