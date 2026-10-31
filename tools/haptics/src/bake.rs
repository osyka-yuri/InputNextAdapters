use crate::archive::{ResourceInfo, ResourceStore};
use crate::bank::{self, Bank, Profile};
use crate::decoder::FfmpegDecoder;
use crate::dsp::{self, VariantEnvelope};
use crate::glob::GlobWork;
use crate::manifest::{Manifest, ManifestProfile};
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::fs;
use std::io::Write;
use std::path::{Component, Path, PathBuf};
use tempfile::NamedTempFile;

const MAX_VARIANTS: usize = 4;
const MAX_FAILURES: usize = 4096;

#[derive(Debug)]
pub struct BakeOutcome {
    pub bank_path: PathBuf,
    pub report_path: PathBuf,
    pub bank: Bank,
    pub report: BakeReport,
}

#[derive(Clone, Debug, Serialize)]
pub struct BakeReport {
    pub version: u32,
    pub game: String,
    pub manifest: String,
    pub ffmpeg: String,
    pub bank_profiles: usize,
    pub bank_sha256: String,
    pub dsp: DspDescription,
    pub coverage: Vec<ProfileCoverage>,
    pub failures: Vec<String>,
}

#[derive(Clone, Debug, Serialize)]
pub struct DspDescription {
    pub sample_rate_hz: u32,
    pub onset_detection: String,
    pub alignment: String,
    pub filter: String,
    pub rms_window_ms: u32,
    pub knot_count: usize,
    pub normalization: String,
    pub maximum_variants_per_profile: usize,
    pub source_timing: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct ProfileCoverage {
    pub section: String,
    pub status: String,
    pub requested_patterns: Vec<String>,
    pub selected_variants: usize,
    pub decoded_variants: usize,
    pub duration_ms: u32,
    pub onset_search_ms: u32,
    pub target_carriers_hz: [f32; 2],
    pub style: String,
    pub mix: f32,
    pub sources: Vec<SourceCoverage>,
}

#[derive(Clone, Debug, Serialize)]
pub struct SourceCoverage {
    pub path: String,
    pub origin: String,
    pub size_bytes: u64,
    pub sha256: String,
    pub decoded: bool,
    pub onset_ms: Option<f32>,
    pub failure: Option<String>,
}

/// Reads selected sources, validates a game-tagged bank, and atomically replaces each output file.
/// The report and bank are committed separately; the pair is not transactional.
///
/// # Errors
/// Returns an error for invalid manifests/resources, strict coverage failures, unsafe output paths, or write failures.
pub fn bake(
    store: &ResourceStore,
    manifest: &Manifest,
    decoder: &FfmpegDecoder,
    ffmpeg_path: &Path,
    manifest_path: &Path,
    output: &Path,
    allow_missing: bool,
) -> Result<BakeOutcome> {
    manifest.validate()?;
    let root = fs::canonicalize(store.data_root()).context("resolve game Data path")?;
    let bank_path = absolute_output(output)?;
    let mut report_path = bank_path.clone();
    let mut report_name = bank_path
        .file_name()
        .context("bank output needs a filename")?
        .to_os_string();
    report_name.push(".json");
    report_path.set_file_name(report_name);
    ensure!(
        !is_within(&bank_path, &root) && !is_within(&report_path, &root),
        "output bank and report must be outside the installed game's Data directory"
    );
    ensure!(
        bank_path != report_path,
        "bank and report output paths collide"
    );
    let mut protected_paths = store
        .archive_paths()
        .map(Path::to_path_buf)
        .collect::<Vec<_>>();
    protected_paths.push(manifest_path.to_path_buf());
    protected_paths.push(ffmpeg_path.to_path_buf());
    ensure!(
        !protected_paths
            .iter()
            .any(|path| same_path(&bank_path, path) || same_path(&report_path, path)),
        "output bank or report would overwrite an input archive, manifest, or FFmpeg executable"
    );

    let (mut profiles, coverage, failures) = collect_profiles(store, manifest, decoder)?;
    if !allow_missing && !failures.is_empty() {
        anyhow::bail!(
            "strict bake rejected {} missing or failed source/profile conditions; use --allow-missing to emit a reported subset:\n{}",
            failures.len(),
            failure_summary(&failures)
        );
    }
    profiles.sort_by(|left, right| left.section.cmp(&right.section));
    let result_bank = Bank {
        game: manifest.game,
        profiles,
    };
    result_bank
        .validate()
        .context("validate generated wire bank")?;
    let bank_bytes = bank::encode(&result_bank)?;
    let report = build_report(
        manifest,
        ffmpeg_path,
        manifest_path,
        &bank_bytes,
        result_bank.profiles.len(),
        coverage,
        failures,
    );
    write_outputs(
        &bank_path,
        &report_path,
        &bank_bytes,
        &report,
        &root,
        &protected_paths,
    )?;
    Ok(BakeOutcome {
        bank_path,
        report_path,
        bank: result_bank,
        report,
    })
}

fn collect_profiles(
    store: &ResourceStore,
    manifest: &Manifest,
    decoder: &FfmpegDecoder,
) -> Result<(Vec<Profile>, Vec<ProfileCoverage>, Vec<String>)> {
    let mut profiles = Vec::new();
    let mut coverage = Vec::with_capacity(manifest.profiles.len());
    let mut failures = Vec::new();
    let mut glob_work = GlobWork::default();
    for spec in &manifest.profiles {
        let (profile, row, profile_failures) =
            analyze_profile(store, decoder, spec, &mut glob_work)?;
        if let Some(profile) = profile {
            profiles.push(profile);
        }
        coverage.push(row);
        for failure in profile_failures {
            record_failure(&mut failures, failure)?;
        }
    }
    Ok((profiles, coverage, failures))
}

fn analyze_profile(
    store: &ResourceStore,
    decoder: &FfmpegDecoder,
    spec: &ManifestProfile,
    glob_work: &mut GlobWork,
) -> Result<(Option<Profile>, ProfileCoverage, Vec<String>)> {
    let variants = store
        .matching(&spec.sources, glob_work)?
        .into_iter()
        .take(MAX_VARIANTS)
        .collect::<Vec<_>>();
    let mut coverage = ProfileCoverage {
        section: spec.section.clone(),
        status: "missing".to_owned(),
        requested_patterns: spec.sources.clone(),
        selected_variants: variants.len(),
        decoded_variants: 0,
        duration_ms: spec.duration_ms,
        onset_search_ms: spec.onset_search_ms,
        target_carriers_hz: [spec.frequency, spec.second_frequency],
        style: style_name(spec),
        mix: spec.mix,
        sources: Vec::new(),
    };
    let mut failures = Vec::new();
    if variants.is_empty() {
        let failure = format!(
            "{}: no resources matched the declared source patterns",
            spec.section
        );
        failures.push(failure.clone());
        coverage.sources.push(SourceCoverage {
            path: String::new(),
            origin: String::new(),
            size_bytes: 0,
            sha256: String::new(),
            decoded: false,
            onset_ms: None,
            failure: Some(failure),
        });
        return Ok((None, coverage, failures));
    }

    let mut analyzed = Vec::with_capacity(variants.len());
    for resource in &variants {
        match process_variant(store, decoder, spec, resource) {
            Ok((variant, hash)) => {
                coverage.decoded_variants += 1;
                coverage.sources.push(SourceCoverage {
                    path: resource.path.clone(),
                    origin: resource.origin.clone(),
                    size_bytes: resource.size_bytes,
                    sha256: hash,
                    decoded: true,
                    onset_ms: Some(variant.onset_ms),
                    failure: None,
                });
                analyzed.push(variant);
            }
            Err(error) => {
                failures.push(format!("{}: {}: {error:#}", spec.section, resource.path));
                coverage.sources.push(SourceCoverage {
                    path: resource.path.clone(),
                    origin: resource.origin.clone(),
                    size_bytes: resource.size_bytes,
                    sha256: String::new(),
                    decoded: false,
                    onset_ms: None,
                    failure: Some(error.to_string()),
                });
            }
        }
    }

    if analyzed.is_empty() {
        failures.push(format!("{}: all selected resources failed", spec.section));
        return Ok((None, coverage, failures));
    }
    let profile = dsp::average_variants(spec, &analyzed)?;
    if analyzed.len() == variants.len() {
        "baked".clone_into(&mut coverage.status);
    } else {
        "partial".clone_into(&mut coverage.status);
    }
    Ok((Some(profile), coverage, failures))
}

fn build_report(
    manifest: &Manifest,
    ffmpeg_path: &Path,
    manifest_path: &Path,
    bank_bytes: &[u8],
    bank_profiles: usize,
    coverage: Vec<ProfileCoverage>,
    failures: Vec<String>,
) -> BakeReport {
    BakeReport {
        version: 1,
        game: manifest.game.to_string(),
        manifest: manifest_path.to_string_lossy().into_owned(),
        ffmpeg: ffmpeg_path.to_string_lossy().into_owned(),
        bank_profiles,
        bank_sha256: sha256_hex(bank_bytes),
        dsp: DspDescription {
            sample_rate_hz: dsp::SAMPLE_RATE_HZ,
            onset_detection: "5 ms RMS windows with 2 ms hop; first window reaching max(7% of search-window peak, 0.0005); per-profile onset_search_ms defaults to 120 ms and is capped at 2000 ms".to_owned(),
            alignment: "crop each onset-aligned cue window at its authored duration; zero-pad a short source window; never time-scale a complete audio tail".to_owned(),
            filter: "offline one-pole low-pass at 700 Hz; lower band is the low-pass output and upper band is the residual".to_owned(),
            rms_window_ms: 8,
            knot_count: bank::KNOTS,
            normalization: format!("average at most {MAX_VARIANTS} sorted source variants after onset alignment, apply authored mix to the secondary band, jointly scale both bands so peak paired sum is {:.2}, force both channel endpoints to zero", dsp::GAIN_CAP),
            maximum_variants_per_profile: MAX_VARIANTS,
            source_timing: "real source samples drive onset and temporal envelopes; authored frequencies are controller carriers, not acoustic measurements".to_owned(),
        },
        coverage,
        failures,
    }
}

fn failure_summary(failures: &[String]) -> String {
    let mut summary = String::new();
    for failure in failures.iter().take(20) {
        if !summary.is_empty() {
            summary.push('\n');
        }
        summary.push_str("- ");
        summary.push_str(failure);
    }
    let remainder = failures.len().saturating_sub(20);
    if remainder > 0 {
        summary.push_str("\n- ... and ");
        summary.push_str(&remainder.to_string());
        summary.push_str(" more failures");
    }
    summary
}

fn sha256_hex(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let digest = Sha256::digest(bytes);
    let mut encoded = String::with_capacity(digest.len() * 2);
    for byte in digest {
        encoded.push(char::from(HEX[usize::from(byte >> 4)]));
        encoded.push(char::from(HEX[usize::from(byte & 0x0f)]));
    }
    encoded
}

fn process_variant(
    store: &ResourceStore,
    decoder: &FfmpegDecoder,
    spec: &ManifestProfile,
    resource: &ResourceInfo,
) -> Result<(VariantEnvelope, String)> {
    let (bytes, origin) = store.read(&resource.path)?;
    ensure!(
        origin == resource.origin,
        "resource override changed while baking: {}",
        resource.path
    );
    let hash = sha256_hex(&bytes);
    let samples = decoder.decode_with_onset(
        &resource.path,
        &bytes,
        spec.duration_ms,
        spec.onset_search_ms,
    )?;
    let envelope = dsp::analyze_variant(&samples, spec)?;
    Ok((envelope, hash))
}

fn style_name(spec: &ManifestProfile) -> String {
    match spec.style {
        crate::manifest::Style::Blunt => "blunt",
        crate::manifest::Style::Whoosh => "whoosh",
        crate::manifest::Style::Single => "single",
        crate::manifest::Style::Resonant => "resonant",
        crate::manifest::Style::Impact => "impact",
        crate::manifest::Style::Recoil => "recoil",
    }
    .to_owned()
}

fn record_failure(failures: &mut Vec<String>, failure: String) -> Result<()> {
    ensure!(
        failures.len() < MAX_FAILURES,
        "failure report exceeds {MAX_FAILURES} rows"
    );
    failures.push(failure);
    Ok(())
}

fn absolute_output(path: &Path) -> Result<PathBuf> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()?.join(path)
    };
    for component in absolute.components() {
        ensure!(
            !matches!(component, Component::ParentDir),
            "output path may not contain '..': {}",
            path.display()
        );
    }
    Ok(absolute)
}

fn is_within(candidate: &Path, root: &Path) -> bool {
    let normalized = normalize_missing_tail(candidate);
    let normalized_root = fs::canonicalize(root).unwrap_or_else(|_| root.to_path_buf());
    #[cfg(windows)]
    {
        let candidate = normalized.to_string_lossy().to_ascii_lowercase();
        let root = normalized_root.to_string_lossy().to_ascii_lowercase();
        candidate == root
            || candidate
                .strip_prefix(&root)
                .is_some_and(|tail| tail.starts_with('\\') || tail.starts_with('/'))
    }
    #[cfg(not(windows))]
    {
        normalized.starts_with(normalized_root)
    }
}

fn normalize_missing_tail(path: &Path) -> PathBuf {
    if path.exists() {
        return fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    }
    let mut cursor = path;
    let mut tail = Vec::new();
    while !cursor.exists() {
        let Some(name) = cursor.file_name() else {
            return path.to_path_buf();
        };
        tail.push(name.to_os_string());
        let Some(parent) = cursor.parent() else {
            return path.to_path_buf();
        };
        cursor = parent;
    }
    let mut result = fs::canonicalize(cursor).unwrap_or_else(|_| cursor.to_path_buf());
    for part in tail.iter().rev() {
        result.push(part);
    }
    result
}

fn same_path(left: &Path, right: &Path) -> bool {
    let left = normalize_missing_tail(left);
    let right = normalize_missing_tail(right);
    #[cfg(windows)]
    {
        left.to_string_lossy()
            .eq_ignore_ascii_case(&right.to_string_lossy())
    }
    #[cfg(not(windows))]
    {
        left == right
    }
}

fn write_outputs(
    bank_path: &Path,
    report_path: &Path,
    bank_bytes: &[u8],
    report: &BakeReport,
    data_root: &Path,
    protected_paths: &[PathBuf],
) -> Result<()> {
    ensure!(
        !is_within(bank_path, data_root) && !is_within(report_path, data_root),
        "refusing to write bake outputs into the installed game's Data directory"
    );
    ensure!(
        !protected_paths
            .iter()
            .any(|path| same_path(bank_path, path) || same_path(report_path, path)),
        "refusing to overwrite an input archive, manifest, or FFmpeg executable"
    );
    let bank_parent = bank_path
        .parent()
        .context("bank output needs a parent directory")?;
    let report_parent = report_path
        .parent()
        .context("report output needs a parent directory")?;
    fs::create_dir_all(bank_parent)
        .with_context(|| format!("create output directory {}", bank_parent.display()))?;
    fs::create_dir_all(report_parent)
        .with_context(|| format!("create report directory {}", report_parent.display()))?;
    // Encode and decode have already validated the bank. Serialize the report before creating
    // either destination so serialization failures cannot leave a partial result.
    let report_bytes = serde_json::to_vec_pretty(report)?;
    let staged_bank = stage_file(bank_parent, bank_bytes)?;
    let staged_report = stage_file(report_parent, &report_bytes)?;
    commit_staged_outputs(
        staged_report,
        report_path,
        staged_bank,
        bank_path,
        persist_staged_file,
    )
}

fn commit_staged_outputs<F>(
    staged_report: NamedTempFile,
    report_path: &Path,
    staged_bank: NamedTempFile,
    bank_path: &Path,
    mut persist: F,
) -> Result<()>
where
    F: FnMut(NamedTempFile, &Path) -> Result<()>,
{
    persist(staged_report, report_path)
        .with_context(|| format!("atomically replace {}", report_path.display()))?;
    persist(staged_bank, bank_path).with_context(|| {
        format!(
            "replace {} failed after {} was updated; the bank may not match the report. Verify the report bank_sha256 before reusing the pair or rerun the bake",
            bank_path.display(),
            report_path.display()
        )
    })?;
    Ok(())
}

fn persist_staged_file(staged: NamedTempFile, path: &Path) -> Result<()> {
    staged.persist(path).map(drop).map_err(anyhow::Error::new)
}

fn stage_file(parent: &Path, bytes: &[u8]) -> Result<NamedTempFile> {
    let mut file = NamedTempFile::new_in(parent)?;
    file.write_all(bytes)?;
    file.as_file().sync_all()?;
    Ok(file)
}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    reason = "Filesystem test fixtures use fail-fast setup to keep atomic-write failures local to the assertion."
)]
mod tests {
    use super::*;
    use crate::bank::{KNOTS, Profile};
    use crate::model::Game;
    use tempfile::tempdir;

    fn valid_bank_bytes() -> Vec<u8> {
        let mut primary = [0.0; KNOTS];
        primary[1] = 0.5;
        let bank = Bank {
            game: Game::SkyrimSe,
            profiles: vec![Profile {
                section: "Body.Test".to_owned(),
                frequency: 78.0,
                second_frequency: 0.0,
                primary,
                secondary: [0.0; KNOTS],
            }],
        };
        bank::encode(&bank).expect("valid test bank")
    }

    fn report() -> BakeReport {
        BakeReport {
            version: 1,
            game: "skyrim-se".to_owned(),
            manifest: "test.json".to_owned(),
            ffmpeg: "ffmpeg.exe".to_owned(),
            bank_profiles: 1,
            bank_sha256: "test".to_owned(),
            dsp: DspDescription {
                sample_rate_hz: 16_000,
                onset_detection: "test".to_owned(),
                alignment: "test".to_owned(),
                filter: "test".to_owned(),
                rms_window_ms: 8,
                knot_count: 32,
                normalization: "test".to_owned(),
                maximum_variants_per_profile: 4,
                source_timing: "test".to_owned(),
            },
            coverage: Vec::new(),
            failures: Vec::new(),
        }
    }

    #[test]
    fn strict_failure_summary_names_resources_and_bounds_the_detail_list() {
        let failures = (0..23)
            .map(|index| format!("Body.Profile{index}: missing source{index}.wav"))
            .collect::<Vec<_>>();
        let summary = failure_summary(&failures);
        assert!(summary.contains("Body.Profile0: missing source0.wav"));
        assert!(summary.contains("Body.Profile19: missing source19.wav"));
        assert!(!summary.contains("Body.Profile20:"));
        assert!(summary.contains("... and 3 more failures"));
    }

    #[test]
    fn outputs_replace_complete_validated_files_atomically() {
        let dir = tempdir().expect("tempdir");
        let data = dir.path().join("Data");
        let output = dir.path().join("artifacts").join("bank.audiohaptics");
        let report_path = PathBuf::from(format!("{}.json", output.display()));
        fs::create_dir_all(&data).expect("Data directory");
        fs::create_dir_all(output.parent().expect("output parent")).expect("output directory");
        fs::write(&output, b"old bank").expect("old bank");
        fs::write(&report_path, b"old report").expect("old report");
        let bytes = valid_bank_bytes();
        bank::decode(&bytes).expect("validate bank before write");
        write_outputs(&output, &report_path, &bytes, &report(), &data, &[])
            .expect("atomic output update");
        assert_eq!(
            bank::decode(&fs::read(&output).expect("new bank"))
                .expect("bank remains valid")
                .profiles
                .len(),
            1
        );
        let json: serde_json::Value =
            serde_json::from_slice(&fs::read(&report_path).expect("new report"))
                .expect("report JSON");
        assert_eq!(json["bank_profiles"], 1);
    }

    #[test]
    fn bank_replace_failure_leaves_old_bank_and_reports_the_detectable_mismatch() {
        let dir = tempdir().expect("tempdir");
        let data = dir.path().join("Data");
        let output = dir.path().join("artifacts").join("bank.audiohaptics");
        let report_path = PathBuf::from(format!("{}.json", output.display()));
        fs::create_dir_all(&data).expect("Data directory");
        fs::create_dir_all(output.parent().expect("output parent")).expect("output directory");
        fs::write(&output, b"old bank").expect("old bank");
        fs::write(&report_path, b"old report").expect("old report");
        let bytes = valid_bank_bytes();
        let mut new_report = report();
        new_report.bank_sha256 = sha256_hex(&bytes);
        let mut replace_calls = 0;

        let staged_bank =
            stage_file(output.parent().expect("output parent"), &bytes).expect("stage bank");
        let report_bytes = serde_json::to_vec_pretty(&new_report).expect("serialize report");
        let staged_report = stage_file(output.parent().expect("output parent"), &report_bytes)
            .expect("stage report");
        let error = commit_staged_outputs(
            staged_report,
            &report_path,
            staged_bank,
            &output,
            |staged, path| {
                replace_calls += 1;
                if path == report_path {
                    persist_staged_file(staged, path)
                } else {
                    drop(staged);
                    anyhow::bail!("injected bank replacement failure")
                }
            },
        )
        .expect_err("the injected bank replacement failure is returned");

        assert_eq!(replace_calls, 2);
        assert!(error.to_string().contains("bank may not match the report"));
        assert_eq!(fs::read(&output).expect("old bank remains"), b"old bank");
        let written_report: serde_json::Value =
            serde_json::from_slice(&fs::read(&report_path).expect("new report"))
                .expect("report remains valid JSON");
        assert_eq!(written_report["bank_sha256"], sha256_hex(&bytes));
        assert_ne!(written_report["bank_sha256"], sha256_hex(b"old bank"));
        assert_eq!(
            fs::read_dir(output.parent().expect("output parent"))
                .expect("output directory remains readable")
                .count(),
            2,
            "both staged files are cleaned up after failure"
        );
    }

    #[test]
    fn report_replace_failure_preserves_both_outputs_and_cleans_staging() {
        let dir = tempdir().expect("tempdir");
        let data = dir.path().join("Data");
        let output = dir.path().join("artifacts").join("bank.audiohaptics");
        let report_path = PathBuf::from(format!("{}.json", output.display()));
        fs::create_dir_all(&data).expect("Data directory");
        fs::create_dir_all(output.parent().expect("output parent")).expect("output directory");
        fs::write(&output, b"old bank").expect("old bank");
        fs::write(&report_path, b"old report").expect("old report");

        let staged_bank = stage_file(output.parent().expect("output parent"), &valid_bank_bytes())
            .expect("stage bank");
        let report_bytes = serde_json::to_vec_pretty(&report()).expect("serialize report");
        let staged_report = stage_file(output.parent().expect("output parent"), &report_bytes)
            .expect("stage report");
        let error = commit_staged_outputs(
            staged_report,
            &report_path,
            staged_bank,
            &output,
            |staged, _path| {
                drop(staged);
                anyhow::bail!("injected report replacement failure")
            },
        )
        .expect_err("the injected report replacement failure is returned");

        assert!(error.to_string().contains("atomically replace"));
        assert_eq!(fs::read(&output).expect("old bank remains"), b"old bank");
        assert_eq!(
            fs::read(&report_path).expect("old report remains"),
            b"old report"
        );
        assert_eq!(
            fs::read_dir(output.parent().expect("output parent"))
                .expect("output directory remains readable")
                .count(),
            2,
            "both staged files are cleaned up after failure"
        );
    }

    #[test]
    fn refuses_targets_inside_game_data_before_creating_output_files() {
        let dir = tempdir().expect("tempdir");
        let data = dir.path().join("Data");
        fs::create_dir_all(&data).expect("Data directory");
        let output = data.join("bank.audiohaptics");
        let report_path = PathBuf::from(format!("{}.json", output.display()));
        assert!(
            write_outputs(
                &output,
                &report_path,
                &valid_bank_bytes(),
                &report(),
                &data,
                &[]
            )
            .is_err()
        );
        assert!(!output.exists() && !report_path.exists());
    }
}
