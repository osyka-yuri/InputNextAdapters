use crate::glob::{GlobSet, GlobWork};
use crate::model::Game;
use crate::{MAX_ARCHIVE_ENTRIES, MAX_RESOURCE_BYTES, MAX_RESOURCE_PATH_BYTES};
use anyhow::{Context, Result, bail, ensure};
use serde::Serialize;
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};
use walkdir::WalkDir;

// Bound the retained merged inventory across all archives and loose overrides. One million
// unique paths permits substantial mod collections; the byte cap limits duplicated metadata.
const MAX_MERGED_RESOURCE_ENTRIES: usize = 1_000_000;
const MAX_MERGED_METADATA_BYTES: usize = 128 * 1024 * 1024;
#[derive(Clone, Debug, Serialize)]
pub struct ResourceInfo {
    pub path: String,
    pub size_bytes: u64,
    pub origin: String,
}

mod formats;
#[cfg(test)]
pub(in crate::archive) mod test_support;

use formats::{ArchiveEntry, parse_archive, read_entry};

#[derive(Debug)]
enum Source {
    Loose(PathBuf),
    Archived(ArchiveEntry),
}

#[derive(Debug)]
struct ResourceRecord {
    info: ResourceInfo,
    source: Source,
}

#[derive(Clone, Copy)]
struct InventoryLimits {
    entries: usize,
    metadata_bytes: usize,
}

impl InventoryLimits {
    const MERGED: Self = Self {
        entries: MAX_MERGED_RESOURCE_ENTRIES,
        metadata_bytes: MAX_MERGED_METADATA_BYTES,
    };
}

#[derive(Debug)]
pub struct ResourceStore {
    root: PathBuf,
    archive_paths: Vec<PathBuf>,
    entries: BTreeMap<String, ResourceRecord>,
    metadata_bytes: usize,
}

impl ResourceStore {
    /// Builds the merged resource view from the default archive, ordered overrides, and loose files.
    ///
    /// # Errors
    /// Returns an error for malformed or unsupported archives, path escapes, unreadable directories, or limit violations.
    pub fn open(game_data: &Path, game: Game, additional_archives: &[PathBuf]) -> Result<Self> {
        let root = resolve_data_root(game_data, game)?;
        let metadata_bytes = path_text_bytes(&root);
        ensure!(
            metadata_bytes <= MAX_MERGED_METADATA_BYTES,
            "merged resource inventory metadata exceeds {MAX_MERGED_METADATA_BYTES} bytes"
        );
        let default_archive = root.join(game.archive_name());
        ensure!(
            default_archive.is_file(),
            "default sound archive not found: {}",
            default_archive.display()
        );
        let mut store = Self {
            root: root.clone(),
            archive_paths: Vec::new(),
            entries: BTreeMap::new(),
            metadata_bytes,
        };
        store.add_archive(&default_archive)?;
        for archive in additional_archives {
            store.add_archive(archive)?;
        }
        store.add_loose_overrides(&root)?;
        Ok(store)
    }

    pub fn resources(&self) -> impl Iterator<Item = &ResourceInfo> {
        self.entries
            .values()
            .filter(|record| is_audio_path(&record.info.path))
            .map(|record| &record.info)
    }

    #[must_use]
    pub fn data_root(&self) -> &Path {
        &self.root
    }

    pub fn archive_paths(&self) -> impl Iterator<Item = &Path> {
        self.archive_paths.iter().map(PathBuf::as_path)
    }

    /// Finds audio resources matching bounded game-path globs.
    ///
    /// # Errors
    /// Returns an error for excessive patterns, resource paths, or cumulative matching work.
    pub(crate) fn matching(
        &self,
        patterns: &[String],
        work: &mut GlobWork,
    ) -> Result<Vec<ResourceInfo>> {
        let mut patterns = GlobSet::parse(patterns)?;
        let mut matched = Vec::new();
        for record in self.entries.values() {
            if !is_audio_path(&record.info.path) {
                continue;
            }
            let found = patterns.matches(&record.info.path, work)?;
            if found {
                matched.push(record.info.clone());
            }
        }
        Ok(matched)
    }

    /// Reads one selected resource without extracting the archive.
    ///
    /// # Errors
    /// Returns an error when the path is absent, escapes the virtual root, exceeds a read bound, or cannot be decoded.
    pub fn read(&self, resource_path: &str) -> Result<(Vec<u8>, String)> {
        let key = normalize_virtual_path(resource_path)?;
        let record = self
            .entries
            .get(&key)
            .with_context(|| format!("resource not found: {resource_path}"))?;
        match &record.source {
            Source::Loose(path) => {
                let metadata =
                    fs::metadata(path).with_context(|| format!("stat {}", path.display()))?;
                ensure!(
                    metadata.len() <= MAX_RESOURCE_BYTES as u64,
                    "resource exceeds 32 MiB limit: {resource_path}"
                );
                let mut file =
                    File::open(path).with_context(|| format!("open {}", path.display()))?;
                let mut bytes = Vec::new();
                file.by_ref()
                    .take(MAX_RESOURCE_BYTES as u64 + 1)
                    .read_to_end(&mut bytes)?;
                ensure!(
                    bytes.len() <= MAX_RESOURCE_BYTES,
                    "resource grew beyond 32 MiB read limit: {resource_path}"
                );
                Ok((bytes, record.info.origin.clone()))
            }
            Source::Archived(entry) => {
                ensure!(
                    entry.info().size_bytes <= MAX_RESOURCE_BYTES as u64,
                    "resource exceeds 32 MiB limit: {}",
                    entry.info().path
                );
                let path = Path::new(&entry.info().origin);
                let mut file =
                    File::open(path).with_context(|| format!("open archive {}", path.display()))?;
                let bytes = read_entry(&mut file, entry)?;
                Ok((bytes, entry.info().origin.clone()))
            }
        }
    }

    fn add_archive(&mut self, path: &Path) -> Result<()> {
        let path = fs::canonicalize(path)
            .with_context(|| format!("resolve archive {}", path.display()))?;
        self.retain_archive_path(path.clone(), InventoryLimits::MERGED)?;
        for entry in parse_archive(&path)? {
            let key = normalize_virtual_path(&entry.info().path)?;
            let info = entry.info().clone();
            self.insert_record(
                key,
                ResourceRecord {
                    info,
                    source: Source::Archived(entry),
                },
                InventoryLimits::MERGED,
            )?;
        }
        Ok(())
    }

    fn retain_archive_path(&mut self, path: PathBuf, limits: InventoryLimits) -> Result<()> {
        let next_metadata_bytes = self
            .metadata_bytes
            .checked_add(path_text_bytes(&path))
            .context("resource inventory metadata byte count overflow")?;
        ensure!(
            next_metadata_bytes <= limits.metadata_bytes,
            "merged resource inventory metadata exceeds {} bytes",
            limits.metadata_bytes
        );
        self.archive_paths.push(path);
        self.metadata_bytes = next_metadata_bytes;
        Ok(())
    }

    fn add_loose_overrides(&mut self, root: &Path) -> Result<()> {
        let mut count = 0usize;
        for item in WalkDir::new(root).follow_links(false) {
            let item = item?;
            if !item.file_type().is_file() || !is_audio_path(&item.file_name().to_string_lossy()) {
                continue;
            }
            count += 1;
            ensure!(
                count <= MAX_ARCHIVE_ENTRIES,
                "loose audio inventory exceeds {MAX_ARCHIVE_ENTRIES} files"
            );
            let canonical_path = fs::canonicalize(item.path())
                .with_context(|| format!("resolve loose path {}", item.path().display()))?;
            ensure!(
                path_is_within(&canonical_path, root),
                "loose resource resolves outside the game's Data directory: {}",
                item.path().display()
            );
            let relative = item
                .path()
                .strip_prefix(root)
                .context("loose override escaped Data directory")?;
            let display_path = display_virtual_path(&relative.to_string_lossy())?;
            let virtual_path = normalize_virtual_path(&display_path)?;
            let metadata = item
                .metadata()
                .with_context(|| format!("stat {}", item.path().display()))?;
            let info = ResourceInfo {
                path: display_path,
                size_bytes: metadata.len(),
                origin: format!("loose:{}", item.path().display()),
            };
            // Loose Data always wins, regardless of archive override order.
            self.insert_record(
                virtual_path,
                ResourceRecord {
                    info,
                    source: Source::Loose(canonical_path),
                },
                InventoryLimits::MERGED,
            )?;
        }
        Ok(())
    }

    fn insert_record(
        &mut self,
        key: String,
        record: ResourceRecord,
        limits: InventoryLimits,
    ) -> Result<()> {
        let previous_metadata_bytes = self
            .entries
            .get(&key)
            .map(|previous| resource_metadata_bytes(&key, previous))
            .transpose()?;
        let next_entry_count = if previous_metadata_bytes.is_some() {
            self.entries.len()
        } else {
            self.entries
                .len()
                .checked_add(1)
                .context("resource inventory entry count overflow")?
        };
        ensure!(
            next_entry_count <= limits.entries,
            "merged resource inventory exceeds {} unique paths",
            limits.entries
        );

        let record_metadata_bytes = resource_metadata_bytes(&key, &record)?;
        let next_metadata_bytes = self
            .metadata_bytes
            .checked_sub(previous_metadata_bytes.unwrap_or_default())
            .context("resource inventory metadata accounting underflow")?
            .checked_add(record_metadata_bytes)
            .context("resource inventory metadata byte count overflow")?;
        ensure!(
            next_metadata_bytes <= limits.metadata_bytes,
            "merged resource inventory metadata exceeds {} bytes",
            limits.metadata_bytes
        );

        self.entries.insert(key, record);
        self.metadata_bytes = next_metadata_bytes;
        Ok(())
    }
}

fn resource_metadata_bytes(key: &str, record: &ResourceRecord) -> Result<usize> {
    let mut bytes = key
        .len()
        .checked_add(record.info.path.len())
        .and_then(|bytes| bytes.checked_add(record.info.origin.len()))
        .context("resource metadata byte count overflow")?;
    match &record.source {
        Source::Loose(path) => {
            bytes = bytes
                .checked_add(path_text_bytes(path))
                .context("resource metadata byte count overflow")?;
        }
        Source::Archived(entry) => {
            bytes = bytes
                .checked_add(entry.info().path.len())
                .and_then(|bytes| bytes.checked_add(entry.info().origin.len()))
                .context("resource metadata byte count overflow")?;
        }
    }
    Ok(bytes)
}

fn path_text_bytes(path: &Path) -> usize {
    path.as_os_str().to_string_lossy().len()
}

fn resolve_data_root(path: &Path, game: Game) -> Result<PathBuf> {
    ensure!(
        path.is_dir(),
        "game-data path is not a directory: {}",
        path.display()
    );
    if path.join(game.archive_name()).is_file()
        || path.join("Sound").is_dir()
        || path.join("sound").is_dir()
    {
        return fs::canonicalize(path)
            .with_context(|| format!("resolve Data directory {}", path.display()));
    }
    let child = path.join("Data");
    if child.is_dir() {
        return fs::canonicalize(&child)
            .with_context(|| format!("resolve Data directory {}", child.display()));
    }
    bail!(
        "game-data must be the game's Data directory or its parent containing Data: {}",
        path.display()
    )
}

fn is_audio_path(path: &str) -> bool {
    Path::new(path).extension().is_some_and(|extension| {
        let extension = extension.to_string_lossy();
        extension.eq_ignore_ascii_case("wav") || extension.eq_ignore_ascii_case("xwm")
    })
}

fn path_is_within(path: &Path, root: &Path) -> bool {
    #[cfg(windows)]
    {
        let candidate = path.to_string_lossy().to_ascii_lowercase();
        let root = root.to_string_lossy().to_ascii_lowercase();
        candidate == root
            || candidate
                .strip_prefix(&root)
                .is_some_and(|tail| tail.starts_with('\\') || tail.starts_with('/'))
    }
    #[cfg(not(windows))]
    {
        path.starts_with(root)
    }
}

fn normalize_virtual_path(path: &str) -> Result<String> {
    Ok(display_virtual_path(path)?.to_ascii_lowercase())
}

fn display_virtual_path(path: &str) -> Result<String> {
    let normalized = path.replace('\\', "/");
    ensure!(
        !normalized.starts_with('/') && !normalized.contains(':') && !normalized.is_empty(),
        "absolute or empty resource path rejected: {path}"
    );
    ensure!(
        normalized.len() <= MAX_RESOURCE_PATH_BYTES,
        "resource path exceeds {MAX_RESOURCE_PATH_BYTES}-byte safety bound"
    );
    let mut parts = Vec::new();
    for part in normalized.split('/') {
        if part.is_empty() {
            continue;
        }
        ensure!(
            part != "." && part != "..",
            "path traversal rejected: {path}"
        );
        parts.push(part);
    }
    ensure!(!parts.is_empty(), "empty resource path rejected: {path}");
    Ok(parts.join("/"))
}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    clippy::cast_possible_truncation,
    reason = "Archive fixtures use fail-fast setup and fixed, small values so malformed builders fail at their source."
)]
mod tests {
    use super::*;
    use crate::archive::test_support::bsa_fixture;
    use tempfile::tempdir;

    fn archived_record(path: &str, origin: &str) -> ResourceRecord {
        let info = ResourceInfo {
            path: path.to_owned(),
            size_bytes: 1,
            origin: origin.to_owned(),
        };
        ResourceRecord {
            info: info.clone(),
            source: Source::Archived(formats::test_support::raw_entry(info)),
        }
    }

    fn loose_record(path: &str, origin: &str) -> ResourceRecord {
        ResourceRecord {
            info: ResourceInfo {
                path: path.to_owned(),
                size_bytes: 1,
                origin: origin.to_owned(),
            },
            source: Source::Loose(PathBuf::from(origin)),
        }
    }

    fn empty_store() -> ResourceStore {
        ResourceStore {
            root: PathBuf::new(),
            archive_paths: Vec::new(),
            entries: BTreeMap::new(),
            metadata_bytes: 0,
        }
    }

    #[test]
    fn later_archives_override_earlier_archives_and_loose_data_wins_last() {
        let dir = tempdir().expect("tempdir");
        let data = dir.path().join("Data");
        fs::create_dir_all(&data).expect("Data");
        bsa_fixture(
            &data.join(Game::SkyrimSe.archive_name()),
            104,
            b"default",
            false,
        );
        let first = dir.path().join("first.bsa");
        let second = dir.path().join("second.bsa");
        bsa_fixture(&first, 105, b"first", false);
        bsa_fixture(&second, 104, b"second", false);
        let store =
            ResourceStore::open(&data, Game::SkyrimSe, &[first, second]).expect("open store");
        assert_eq!(
            store.read("sound/pulse.wav").expect("read override").0,
            b"second"
        );
        let loose = data.join("Sound").join("Pulse.wav");
        fs::create_dir_all(loose.parent().expect("parent")).expect("loose directory");
        fs::write(&loose, b"loose wins").expect("loose file");
        let store = ResourceStore::open(&data, Game::SkyrimSe, &[]).expect("open loose store");
        assert_eq!(
            store.read("SOUND/pulse.WAV").expect("read loose").0,
            b"loose wins"
        );
    }

    #[test]
    fn merged_entry_limit_counts_unique_paths_and_allows_overrides_at_capacity() {
        let limits = InventoryLimits {
            entries: 1,
            metadata_bytes: usize::MAX,
        };
        let mut store = empty_store();
        store
            .insert_record(
                "sound/pulse.wav".to_owned(),
                archived_record("Sound/Pulse.wav", "first.bsa"),
                limits,
            )
            .expect("insert first archive entry");

        store
            .insert_record(
                "sound/pulse.wav".to_owned(),
                archived_record("Sound/Pulse.wav", "later.bsa"),
                limits,
            )
            .expect("replacement does not increase unique entry count");
        store
            .insert_record(
                "sound/pulse.wav".to_owned(),
                loose_record("Sound/Pulse.wav", "loose:Data/Sound/Pulse.wav"),
                limits,
            )
            .expect("loose override replaces the archived record at capacity");

        let previous_origin = store
            .entries
            .get("sound/pulse.wav")
            .expect("loose record retained")
            .info
            .origin
            .clone();
        let error = store
            .insert_record(
                "sound/other.wav".to_owned(),
                archived_record("Sound/Other.wav", "another.bsa"),
                limits,
            )
            .expect_err("second unique path exceeds the budget");

        assert!(error.to_string().contains("exceeds 1 unique paths"));
        assert_eq!(store.entries.len(), 1);
        let retained = store
            .entries
            .get("sound/pulse.wav")
            .expect("replaced entry remains");
        assert_eq!(retained.info.origin, previous_origin);
        assert!(matches!(&retained.source, Source::Loose(_)));
    }

    #[test]
    fn merged_metadata_limit_accounts_for_replacements_and_rejects_atomically() {
        let key = "sound/pulse.wav";
        let original = archived_record("Sound/Pulse.wav", "archive/");
        let replacement = loose_record("Sound/Pulse.wav", "x");
        let second_key = "sound/other.wav";
        let second = archived_record("Sound/Other.wav", "y");
        let original_bytes = resource_metadata_bytes(key, &original).expect("original metadata");
        let replacement_bytes =
            resource_metadata_bytes(key, &replacement).expect("replacement metadata");
        let second_bytes = resource_metadata_bytes(second_key, &second).expect("second metadata");
        assert!(replacement_bytes < original_bytes);

        let limits = InventoryLimits {
            entries: 3,
            metadata_bytes: original_bytes + second_bytes,
        };
        let mut store = empty_store();
        store
            .insert_record(key.to_owned(), original, limits)
            .expect("insert original");

        let oversized_replacement = archived_record("Sound/Pulse.wav", &"z".repeat(1024));
        let error = store
            .insert_record(key.to_owned(), oversized_replacement, limits)
            .expect_err("oversized replacement exceeds metadata limit");
        assert!(error.to_string().contains("metadata exceeds"));
        assert_eq!(store.metadata_bytes, original_bytes);
        assert_eq!(
            store
                .entries
                .get(key)
                .expect("original retained")
                .info
                .origin,
            "archive/"
        );

        store
            .insert_record(key.to_owned(), replacement, limits)
            .expect("smaller override releases the old metadata budget");
        assert_eq!(store.metadata_bytes, replacement_bytes);
        store
            .insert_record(second_key.to_owned(), second, limits)
            .expect("released bytes permit another resource");
        assert_eq!(store.metadata_bytes, replacement_bytes + second_bytes);

        let error = store
            .insert_record(
                "sound/third.wav".to_owned(),
                archived_record("Sound/Third.wav", "z"),
                limits,
            )
            .expect_err("new unique metadata exceeds the exact budget");
        assert!(error.to_string().contains("metadata exceeds"));
        assert_eq!(store.entries.len(), 2);
        assert_eq!(store.metadata_bytes, replacement_bytes + second_bytes);
        assert_eq!(
            store
                .entries
                .get(key)
                .expect("override remains")
                .info
                .origin,
            "x"
        );
    }

    #[test]
    fn merged_metadata_limit_includes_every_retained_archive_path() {
        let first = PathBuf::from("archive-a.bsa");
        let second = PathBuf::from("archive-b.bsa");
        let metadata_limit = path_text_bytes(&first) + path_text_bytes(&second);
        let limits = InventoryLimits {
            entries: 0,
            metadata_bytes: metadata_limit,
        };
        let mut store = empty_store();

        store
            .retain_archive_path(first.clone(), limits)
            .expect("retain first archive path");
        store
            .retain_archive_path(second.clone(), limits)
            .expect("retain a second archive without an arbitrary count cap");
        assert_eq!(store.archive_paths, [first, second]);
        assert_eq!(store.metadata_bytes, metadata_limit);

        let error = store
            .retain_archive_path(PathBuf::from("archive-c.bsa"), limits)
            .expect_err("third path exceeds the aggregate retained string budget");
        assert!(error.to_string().contains("metadata exceeds"));
        assert_eq!(store.archive_paths.len(), 2);
        assert_eq!(store.metadata_bytes, metadata_limit);
    }
}
