use crate::bank::MAX_RECORDS;
use crate::glob::{MAX_GLOB_PATTERN_BYTES, MAX_GLOB_PATTERNS};
use crate::model::Game;
use anyhow::{Result, ensure};
use serde::Deserialize;
use std::fs;
use std::path::Path;

pub(crate) const DEFAULT_ONSET_SEARCH_MS: u32 = 120;
pub(crate) const MAX_ONSET_SEARCH_MS: u32 = 2000;

const fn default_onset_search_ms() -> u32 {
    DEFAULT_ONSET_SEARCH_MS
}

#[derive(Clone, Copy, Debug, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Style {
    Blunt,
    Whoosh,
    Single,
    Resonant,
    Impact,
    Recoil,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub version: u32,
    pub game: Game,
    pub profiles: Vec<ManifestProfile>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ManifestProfile {
    pub section: String,
    pub duration_ms: u32,
    #[serde(default = "default_onset_search_ms")]
    pub onset_search_ms: u32,
    pub frequency: f32,
    #[serde(default)]
    pub second_frequency: f32,
    #[serde(default)]
    pub mix: f32,
    pub style: Style,
    pub sources: Vec<String>,
}

impl Manifest {
    /// Reads and validates a bounded versioned JSON manifest.
    ///
    /// # Errors
    /// Returns an error for unreadable, oversized, malformed, unsupported, or invalid manifests.
    pub fn read(path: &Path) -> Result<Self> {
        let metadata = fs::metadata(path)?;
        ensure!(
            metadata.len() <= 1024 * 1024,
            "manifest exceeds 1 MiB limit"
        );
        let manifest: Self = serde_json::from_slice(&fs::read(path)?)?;
        manifest.validate()?;
        Ok(manifest)
    }

    /// Checks profile names, carriers, durations, source patterns, and style-specific fields.
    ///
    /// # Errors
    /// Returns an error if any manifest or profile value violates the contract.
    pub fn validate(&self) -> Result<()> {
        ensure!(
            self.version == 1,
            "unsupported manifest version {}",
            self.version
        );
        ensure!(
            !self.profiles.is_empty() && self.profiles.len() <= MAX_RECORDS,
            "manifest must contain 1..={MAX_RECORDS} profiles"
        );
        let mut sections = self
            .profiles
            .iter()
            .map(|profile| profile.section.as_str())
            .collect::<Vec<_>>();
        sections.sort_unstable();
        ensure!(
            sections.windows(2).all(|pair| pair[0] != pair[1]),
            "manifest profile sections must be unique"
        );
        for profile in &self.profiles {
            profile.validate()?;
        }
        Ok(())
    }
}

impl ManifestProfile {
    fn validate(&self) -> Result<()> {
        ensure!(
            !self.section.is_empty()
                && self.section.len() <= 96
                && self
                    .section
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_')),
            "invalid section name: {}",
            self.section
        );
        ensure!(
            (1..=1000).contains(&self.duration_ms),
            "duration_ms must be 1..=1000 for {}",
            self.section
        );
        ensure!(
            (5..=MAX_ONSET_SEARCH_MS).contains(&self.onset_search_ms),
            "onset_search_ms must be 5..={MAX_ONSET_SEARCH_MS} for {}",
            self.section
        );
        ensure!(
            self.frequency.is_finite() && (20.0..=1000.0).contains(&self.frequency),
            "frequency must be finite and 20..=1000 Hz for {}",
            self.section
        );
        ensure!(
            self.second_frequency.is_finite()
                && (self.second_frequency == 0.0
                    || (20.0..=1000.0).contains(&self.second_frequency)),
            "second_frequency must be zero or 20..=1000 Hz for {}",
            self.section
        );
        ensure!(
            self.mix.is_finite() && (0.0..=1.0).contains(&self.mix),
            "mix must be 0..=1 for {}",
            self.section
        );
        ensure!(
            !self.sources.is_empty() && self.sources.len() <= MAX_GLOB_PATTERNS,
            "profile needs 1..={MAX_GLOB_PATTERNS} source patterns: {}",
            self.section
        );
        for source in &self.sources {
            ensure!(
                !source.is_empty()
                    && source.len() <= MAX_GLOB_PATTERN_BYTES
                    && !source.starts_with('/')
                    && !source.contains(':')
                    && !source
                        .replace('\\', "/")
                        .split('/')
                        .any(|part| part == ".."),
                "empty, oversized, absolute, or traversal source pattern rejected: {source}"
            );
        }
        match self.style {
            Style::Blunt | Style::Whoosh | Style::Single => {
                ensure!(
                    self.mix == 0.0,
                    "single-band style requires mix=0 for {}",
                    self.section
                );
            }
            Style::Resonant | Style::Impact | Style::Recoil => {
                ensure!(
                    self.second_frequency != 0.0 && self.mix > 0.0,
                    "two-band style needs a secondary carrier and nonzero mix for {}",
                    self.section
                );
            }
        }
        Ok(())
    }
}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    reason = "Validation tests use expect_err to make an unexpected acceptance fail at the exact invalid input."
)]
mod tests {
    use super::*;

    fn valid_manifest() -> Manifest {
        Manifest {
            version: 1,
            game: Game::Fallout4,
            profiles: vec![ManifestProfile {
                section: "Body.Test".to_owned(),
                duration_ms: 70,
                onset_search_ms: DEFAULT_ONSET_SEARCH_MS,
                frequency: 78.0,
                second_frequency: 0.0,
                mix: 0.0,
                style: Style::Single,
                sources: vec!["sound/*.wav".to_owned()],
            }],
        }
    }

    #[test]
    fn manifest_rejects_glob_inputs_over_authoring_bounds() {
        let mut manifest = valid_manifest();
        manifest.profiles[0].sources[0] = "a".repeat(MAX_GLOB_PATTERN_BYTES + 1);
        let error = manifest.validate().expect_err("oversized glob pattern");
        assert!(error.to_string().contains("oversized"));

        let mut manifest = valid_manifest();
        manifest.profiles[0].sources = vec!["a".to_owned(); MAX_GLOB_PATTERNS + 1];
        let error = manifest.validate().expect_err("too many glob patterns");
        assert!(error.to_string().contains("source patterns"));
    }

    #[test]
    fn onset_search_defaults_to_legacy_window_and_rejects_excessive_bounds() {
        let text = r#"{"version":1,"game":"fallout4","profiles":[{"section":"Body.Test","duration_ms":70,"frequency":78.0,"style":"single","sources":["sound/*.wav"]}]}"#;
        let mut manifest: Manifest = serde_json::from_str(text).expect("legacy manifest");
        assert_eq!(manifest.profiles[0].onset_search_ms, 120);
        assert!(manifest.validate().is_ok());
        for invalid in [0, 4, 2001, u32::MAX] {
            manifest.profiles[0].onset_search_ms = invalid;
            assert!(manifest.validate().is_err());
        }
        for valid in [5, 120, 2000] {
            manifest.profiles[0].onset_search_ms = valid;
            assert!(manifest.validate().is_ok());
        }
    }
}
