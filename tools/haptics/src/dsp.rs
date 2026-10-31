use crate::bank::{KNOTS, Profile};
use crate::manifest::{MAX_ONSET_SEARCH_MS, ManifestProfile, Style};
use anyhow::{Context, Result, ensure};

pub const SAMPLE_RATE: usize = 16_000;
pub const SAMPLE_RATE_HZ: u32 = 16_000;
pub const GAIN_CAP: f32 = 0.88;
const SAMPLE_RATE_F32: f32 = 16_000.0;
const RMS_WINDOW_MS: usize = 8;
const LOW_PASS_HZ: f32 = 700.0;
const SILENCE_FLOOR: f32 = 0.0005;

#[derive(Clone, Debug)]
pub struct VariantEnvelope {
    pub onset_ms: f32,
    pub primary: [f32; KNOTS],
    pub secondary: [f32; KNOTS],
}

/// Finds a bounded onset-aligned temporal envelope from one decoded source variant.
///
/// # Errors
/// Returns an error for empty, nonfinite, silent, or inaudible cue-window samples.
pub fn analyze_variant(samples: &[f32], profile: &ManifestProfile) -> Result<VariantEnvelope> {
    ensure!(!samples.is_empty(), "decoder returned no audio samples");
    ensure!(
        samples.iter().all(|sample| sample.is_finite()),
        "decoded audio contains nonfinite samples"
    );
    ensure!(
        (5..=MAX_ONSET_SEARCH_MS).contains(&profile.onset_search_ms),
        "onset search exceeds supported bounds"
    );
    let onset = find_onset(samples, usize::try_from(profile.onset_search_ms)?);
    ensure!(
        onset.is_some(),
        "decoded audio is silent or has no detectable onset"
    );
    let onset = onset.unwrap_or(0);
    let duration_samples = (profile.duration_ms as usize * SAMPLE_RATE) / 1000;
    ensure!(duration_samples >= 2, "cue duration is too short");
    let mut segment = vec![0.0_f32; duration_samples];
    let available = samples.len().saturating_sub(onset).min(duration_samples);
    segment[..available].copy_from_slice(&samples[onset..onset + available]);

    let (low, high) = split_bands(&segment);
    let broad = segment.clone();
    let window_samples = (RMS_WINDOW_MS * SAMPLE_RATE / 1000).max(2);
    let mut primary = [0.0_f32; KNOTS];
    let mut secondary = [0.0_f32; KNOTS];
    for knot in 0..KNOTS {
        let center = knot * (duration_samples - 1) / (KNOTS - 1);
        let begin = center.saturating_sub(window_samples / 2);
        let end = (begin + window_samples).min(duration_samples);
        let (p, s) = match profile.style {
            Style::Blunt | Style::Whoosh | Style::Single => (rms(&broad[begin..end]), 0.0),
            Style::Resonant => (rms(&high[begin..end]), rms(&low[begin..end]) * profile.mix),
            Style::Impact | Style::Recoil => {
                (rms(&low[begin..end]), rms(&high[begin..end]) * profile.mix)
            }
        };
        primary[knot] = p;
        secondary[knot] = s;
    }
    primary[0] = 0.0;
    primary[KNOTS - 1] = 0.0;
    secondary[0] = 0.0;
    secondary[KNOTS - 1] = 0.0;
    let active_peak = primary
        .iter()
        .chain(secondary.iter())
        .copied()
        .fold(0.0_f32, f32::max);
    ensure!(
        active_peak > SILENCE_FLOOR,
        "onset-aligned cue window contains no usable energy"
    );
    Ok(VariantEnvelope {
        onset_ms: f32::from(u16::try_from(onset).context("detected onset exceeds 65535 samples")?)
            * 1000.0
            / SAMPLE_RATE_F32,
        primary,
        secondary,
    })
}

/// Averages source variants and normalizes both component envelopes together.
///
/// # Errors
/// Returns an error for an empty or oversized source set or an invalid silent average.
pub fn average_variants(
    profile: &ManifestProfile,
    variants: &[VariantEnvelope],
) -> Result<Profile> {
    ensure!(
        !variants.is_empty() && variants.len() <= 4,
        "source average must contain 1..=4 variants"
    );
    let divisor = f32::from(u8::try_from(variants.len()).context("variant count exceeds 255")?);
    let mut primary = [0.0_f32; KNOTS];
    let mut secondary = [0.0_f32; KNOTS];
    for variant in variants {
        for index in 0..KNOTS {
            primary[index] += variant.primary[index] / divisor;
            secondary[index] += variant.secondary[index] / divisor;
        }
    }
    let peak = primary
        .iter()
        .zip(secondary.iter())
        .map(|(first, second)| first + second)
        .fold(0.0_f32, f32::max);
    ensure!(
        peak.is_finite() && peak > SILENCE_FLOOR,
        "averaged source envelopes are silent"
    );
    let scale = GAIN_CAP / peak;
    for value in &mut primary {
        *value = (*value * scale).clamp(0.0, 1.0);
    }
    for value in &mut secondary {
        *value = (*value * scale).clamp(0.0, 1.0);
    }
    primary[0] = 0.0;
    primary[KNOTS - 1] = 0.0;
    secondary[0] = 0.0;
    secondary[KNOTS - 1] = 0.0;
    Ok(Profile {
        section: profile.section.clone(),
        frequency: profile.frequency,
        second_frequency: profile.second_frequency,
        primary,
        secondary,
    })
}

fn find_onset(samples: &[f32], maximum_onset_ms: usize) -> Option<usize> {
    let frame = SAMPLE_RATE * 5 / 1000;
    let hop = SAMPLE_RATE * 2 / 1000;
    let limit = samples
        .len()
        .min(maximum_onset_ms * SAMPLE_RATE / 1000 + frame);
    let mut levels = Vec::new();
    let mut cursor = 0;
    while cursor + frame <= limit {
        levels.push((cursor, rms(&samples[cursor..cursor + frame])));
        cursor += hop;
    }
    let peak = levels
        .iter()
        .map(|(_, level)| *level)
        .fold(0.0_f32, f32::max);
    if peak <= SILENCE_FLOOR {
        return None;
    }
    let threshold = (peak * 0.07).max(SILENCE_FLOOR);
    levels
        .into_iter()
        .find(|(_, level)| *level >= threshold)
        .map(|(at, _)| at)
}

fn split_bands(samples: &[f32]) -> (Vec<f32>, Vec<f32>) {
    let alpha = 1.0 - (-2.0 * std::f32::consts::PI * LOW_PASS_HZ / SAMPLE_RATE_F32).exp();
    let mut low = Vec::with_capacity(samples.len());
    let mut high = Vec::with_capacity(samples.len());
    let mut state = 0.0_f32;
    for sample in samples {
        state += alpha * (*sample - state);
        low.push(state);
        high.push(*sample - state);
    }
    (low, high)
}

#[allow(
    clippy::cast_precision_loss,
    reason = "RMS is only called on fixed 5 ms and 8 ms windows of at most 128 samples."
)]
fn rms(samples: &[f32]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }
    (samples.iter().map(|sample| sample * sample).sum::<f32>() / samples.len() as f32).sqrt()
}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    clippy::cast_precision_loss,
    reason = "DSP tests use fixed, bounded sample indices and fail-fast fixture setup."
)]
mod tests {
    use super::*;
    use crate::manifest::ManifestProfile;

    fn manifest_profile(style: Style, mix: f32) -> ManifestProfile {
        ManifestProfile {
            section: "Body.Impact".to_owned(),
            duration_ms: 80,
            onset_search_ms: crate::manifest::DEFAULT_ONSET_SEARCH_MS,
            frequency: 76.0,
            second_frequency: if mix > 0.0 { 120.0 } else { 0.0 },
            mix,
            style,
            sources: vec!["sound/fx/test.wav".to_owned()],
        }
    }

    #[test]
    fn onset_alignment_removes_leading_silence_and_preserves_short_attack_and_decay() {
        let mut samples = vec![0.0; SAMPLE_RATE / 20];
        samples.extend((0..SAMPLE_RATE / 20).map(|i| {
            if i < 20 {
                0.8
            } else {
                0.25 * (-(i as f32) / 500.0).exp()
            }
        }));
        let analyzed = analyze_variant(&samples, &manifest_profile(Style::Single, 0.0))
            .expect("non-silent cue");
        assert!((45.0..=56.0).contains(&analyzed.onset_ms));
        assert_eq!(analyzed.primary[0], 0.0);
        assert_eq!(analyzed.primary[KNOTS - 1], 0.0);
        let max_index = analyzed
            .primary
            .iter()
            .enumerate()
            .max_by(|a, b| a.1.total_cmp(b.1))
            .map(|(index, _)| index)
            .expect("knots");
        assert!(
            max_index < KNOTS / 2,
            "attack should stay at the beginning rather than being time-squeezed"
        );
        assert!(analyzed.primary[KNOTS / 2] > 0.0);
    }

    #[test]
    fn silence_is_rejected_and_shared_normalization_respects_gain_cap() {
        let silent = vec![0.0; SAMPLE_RATE];
        assert!(analyze_variant(&silent, &manifest_profile(Style::Single, 0.0)).is_err());
        let mut pulse = vec![0.0; SAMPLE_RATE / 4];
        pulse[5..100].fill(0.8);
        let first = analyze_variant(&pulse, &manifest_profile(Style::Impact, 0.4)).expect("pulse");
        let profile =
            average_variants(&manifest_profile(Style::Impact, 0.4), &[first]).expect("average");
        let max_sum = profile
            .primary
            .iter()
            .zip(profile.secondary.iter())
            .map(|(a, b)| a + b)
            .fold(0.0, f32::max);
        assert!((max_sum - GAIN_CAP).abs() < 1e-5);
        assert!(profile.secondary.iter().any(|value| *value > 0.0));
    }

    #[test]
    fn single_band_styles_do_not_invent_a_resonant_component() {
        let mut samples = vec![0.0; 500];
        samples.extend((0..2000).map(|i| (i as f32 * 0.12).sin() * 0.5));
        let result =
            analyze_variant(&samples, &manifest_profile(Style::Blunt, 0.0)).expect("pulse");
        assert!(result.secondary.iter().all(|value| *value == 0.0));
        assert!(result.primary.iter().any(|value| *value > 0.0));
    }

    #[test]
    fn late_source_onset_requires_an_explicit_bounded_search_window() {
        let mut samples = vec![0.0; SAMPLE_RATE * 2];
        for (index, sample) in samples
            .iter_mut()
            .enumerate()
            .skip(SAMPLE_RATE * 3 / 4)
            .take(SAMPLE_RATE / 4)
        {
            *sample = (index as f32 * 0.12).sin() * 0.5;
        }
        let mut profile = manifest_profile(Style::Single, 0.0);
        assert!(analyze_variant(&samples, &profile).is_err());
        profile.onset_search_ms = 2000;
        let result = analyze_variant(&samples, &profile).expect("late source is audible");
        assert!((740.0..=760.0).contains(&result.onset_ms));
        assert!(result.primary.iter().any(|value| *value > 0.0));
        profile.onset_search_ms = MAX_ONSET_SEARCH_MS + 1;
        assert!(analyze_variant(&samples, &profile).is_err());
    }
}
