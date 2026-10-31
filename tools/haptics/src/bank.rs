use crate::model::Game;
use anyhow::{Context, Result, ensure};
use serde::Serialize;

pub const KNOTS: usize = 32;
pub const MAX_RECORDS: usize = 128;
pub const MAX_PAYLOAD: usize = 65_536;
const HEADER_BYTES: usize = 24;

#[derive(Clone, Debug, Serialize)]
pub struct Profile {
    pub section: String,
    pub frequency: f32,
    pub second_frequency: f32,
    pub primary: [f32; KNOTS],
    pub secondary: [f32; KNOTS],
}

#[derive(Clone, Debug)]
pub struct Bank {
    pub game: Game,
    pub profiles: Vec<Profile>,
}

impl Profile {
    /// Checks the frozen v1 profile bounds.
    ///
    /// # Errors
    /// Returns an error for invalid names, carriers, gains, endpoints, or silent envelopes.
    pub fn validate(&self) -> Result<()> {
        validate_section(&self.section)?;
        ensure!(
            self.frequency.is_finite() && (20.0..=1000.0).contains(&self.frequency),
            "invalid primary carrier for {}",
            self.section
        );
        ensure!(
            self.second_frequency.is_finite()
                && (self.second_frequency == 0.0
                    || (20.0..=1000.0).contains(&self.second_frequency)),
            "invalid secondary carrier for {}",
            self.section
        );
        ensure!(
            self.primary[0] == 0.0
                && self.primary[KNOTS - 1] == 0.0
                && self.secondary[0] == 0.0
                && self.secondary[KNOTS - 1] == 0.0,
            "envelope endpoints must be zero for {}",
            self.section
        );
        let mut active = false;
        for (primary, secondary) in self.primary.iter().zip(self.secondary.iter()) {
            ensure!(
                primary.is_finite()
                    && secondary.is_finite()
                    && *primary >= 0.0
                    && *secondary >= 0.0
                    && *primary <= 1.0
                    && *secondary <= 1.0
                    && *primary + *secondary <= 1.0 + 1e-6,
                "invalid envelope value for {}",
                self.section
            );
            active |= *primary > 0.0 || *secondary > 0.0;
        }
        ensure!(
            self.second_frequency != 0.0 || self.secondary.iter().all(|value| *value == 0.0),
            "zero secondary carrier requires a zero secondary envelope for {}",
            self.section
        );
        ensure!(active, "silent profile rejected: {}", self.section);
        Ok(())
    }
}

impl Bank {
    /// Checks count, ordering, uniqueness, and every profile.
    ///
    /// # Errors
    /// Returns an error for an empty/oversized bank, unsorted or duplicate sections, or an invalid profile.
    pub fn validate(&self) -> Result<()> {
        ensure!(
            !self.profiles.is_empty() && self.profiles.len() <= MAX_RECORDS,
            "bank must contain 1..={MAX_RECORDS} profiles"
        );
        let mut previous: Option<&str> = None;
        for profile in &self.profiles {
            profile.validate()?;
            if let Some(previous) = previous {
                ensure!(
                    previous < profile.section.as_str(),
                    "profiles must be unique and sorted by section"
                );
            }
            previous = Some(&profile.section);
        }
        Ok(())
    }
}

/// Encodes the frozen, little-endian v1 runtime bank.
///
/// # Errors
/// Returns an error if the bank is invalid or exceeds the wire payload limit.
pub fn encode(bank: &Bank) -> Result<Vec<u8>> {
    bank.validate()?;
    let mut payload = Vec::new();
    for profile in &bank.profiles {
        let name = profile.section.as_bytes();
        payload.extend_from_slice(
            &u16::try_from(name.len())
                .context("section name length does not fit the wire field")?
                .to_le_bytes(),
        );
        payload.extend_from_slice(name);
        payload.extend_from_slice(&profile.frequency.to_le_bytes());
        payload.extend_from_slice(&profile.second_frequency.to_le_bytes());
        for value in profile.primary.iter().chain(profile.secondary.iter()) {
            payload.extend_from_slice(&value.to_le_bytes());
        }
    }
    ensure!(
        payload.len() <= MAX_PAYLOAD,
        "bank payload exceeds 65536 bytes"
    );
    let mut bytes = Vec::with_capacity(HEADER_BYTES + payload.len());
    bytes.extend_from_slice(b"INHB");
    bytes.extend_from_slice(&1_u32.to_le_bytes());
    bytes.extend_from_slice(&bank.game.tag().to_le_bytes());
    bytes.extend_from_slice(
        &u32::try_from(bank.profiles.len())
            .context("profile count does not fit the wire field")?
            .to_le_bytes(),
    );
    bytes.extend_from_slice(
        &u32::try_from(payload.len())
            .context("payload length does not fit the wire field")?
            .to_le_bytes(),
    );
    bytes.extend_from_slice(&crc32(&payload).to_le_bytes());
    bytes.extend_from_slice(&payload);
    Ok(bytes)
}

/// Validates and decodes a complete v1 bank without accepting partial records or trailing bytes.
///
/// # Errors
/// Returns an error for any header, checksum, length, game tag, profile, or envelope violation.
pub fn decode(bytes: &[u8]) -> Result<Bank> {
    ensure!(bytes.len() >= HEADER_BYTES, "bank header is truncated");
    ensure!(&bytes[..4] == b"INHB", "invalid bank magic");
    let version = read_u32(bytes, 4)?;
    ensure!(version == 1, "unsupported bank version {version}");
    let tag = read_u32(bytes, 8)?;
    let game = match tag {
        1 => Game::SkyrimSe,
        2 => Game::SkyrimLe,
        3 => Game::Fallout4,
        _ => anyhow::bail!("invalid game tag {tag}"),
    };
    let count = read_u32(bytes, 12)? as usize;
    let payload_size = read_u32(bytes, 16)? as usize;
    let checksum = read_u32(bytes, 20)?;
    ensure!(
        (1..=MAX_RECORDS).contains(&count),
        "invalid profile count {count}"
    );
    ensure!(
        payload_size <= MAX_PAYLOAD,
        "bank payload exceeds 65536 bytes"
    );
    ensure!(
        bytes.len() == HEADER_BYTES + payload_size,
        "bank length does not match header or has trailing bytes"
    );
    let payload = &bytes[HEADER_BYTES..];
    ensure!(crc32(payload) == checksum, "bank CRC32 mismatch");
    let mut cursor = 0usize;
    let mut profiles = Vec::with_capacity(count);
    for _ in 0..count {
        let name_len = read_u16(payload, cursor)? as usize;
        cursor = cursor
            .checked_add(2)
            .ok_or_else(|| anyhow::anyhow!("record offset overflow"))?;
        ensure!(
            (1..=96).contains(&name_len),
            "invalid profile name length {name_len}"
        );
        let end = cursor
            .checked_add(name_len)
            .ok_or_else(|| anyhow::anyhow!("record offset overflow"))?;
        ensure!(end <= payload.len(), "truncated profile name");
        let section = std::str::from_utf8(&payload[cursor..end])?.to_owned();
        cursor = end;
        let frequency = read_f32(payload, cursor)?;
        cursor += 4;
        let second_frequency = read_f32(payload, cursor)?;
        cursor += 4;
        let mut primary = [0.0_f32; KNOTS];
        let mut secondary = [0.0_f32; KNOTS];
        for value in primary.iter_mut().chain(secondary.iter_mut()) {
            *value = read_f32(payload, cursor)?;
            cursor += 4;
        }
        profiles.push(Profile {
            section,
            frequency,
            second_frequency,
            primary,
            secondary,
        });
    }
    ensure!(
        cursor == payload.len(),
        "payload contains extra or partial records"
    );
    let bank = Bank { game, profiles };
    bank.validate()?;
    Ok(bank)
}

fn validate_section(section: &str) -> Result<()> {
    ensure!(
        !section.is_empty() && section.len() <= 96,
        "profile section must be 1..=96 ASCII bytes"
    );
    ensure!(
        section
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'.')),
        "invalid profile section: {section}"
    );
    Ok(())
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16> {
    ensure!(
        offset.checked_add(2).is_some_and(|end| end <= bytes.len()),
        "truncated bank record"
    );
    Ok(u16::from_le_bytes(bytes[offset..offset + 2].try_into()?))
}
fn read_u32(bytes: &[u8], offset: usize) -> Result<u32> {
    ensure!(
        offset.checked_add(4).is_some_and(|end| end <= bytes.len()),
        "truncated bank header"
    );
    Ok(u32::from_le_bytes(bytes[offset..offset + 4].try_into()?))
}
fn read_f32(bytes: &[u8], offset: usize) -> Result<f32> {
    ensure!(
        offset.checked_add(4).is_some_and(|end| end <= bytes.len()),
        "truncated envelope"
    );
    Ok(f32::from_le_bytes(bytes[offset..offset + 4].try_into()?))
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = 0xffff_ffff_u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xedb8_8320
            } else {
                crc >> 1
            };
        }
    }
    !crc
}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    reason = "Wire fixtures use fail-fast setup so invalid test data cannot be mistaken for a codec failure."
)]
mod tests {
    use super::*;

    fn profile(section: &str) -> Profile {
        let mut primary = [0.0; KNOTS];
        let mut secondary = [0.0; KNOTS];
        primary[1] = 0.6;
        primary[2] = 0.3;
        secondary[1] = 0.2;
        Profile {
            section: section.to_owned(),
            frequency: 80.0,
            second_frequency: 120.0,
            primary,
            secondary,
        }
    }

    #[test]
    fn wire_round_trip_and_crc_matches_standard_vector() {
        assert_eq!(crc32(b"123456789"), 0xcbf4_3926);
        let bank = Bank {
            game: Game::SkyrimSe,
            profiles: vec![profile("Body.Test")],
        };
        let bytes = encode(&bank).expect("valid fixture encodes");
        let decoded = decode(&bytes).expect("encoded bank decodes");
        assert_eq!(decoded.game, Game::SkyrimSe);
        assert_eq!(decoded.profiles[0].section, "Body.Test");
        assert_eq!(decoded.profiles[0].primary[1], 0.6);
    }

    #[test]
    fn rejects_corruption_truncation_and_trailing_bytes() {
        let bytes = encode(&Bank {
            game: Game::Fallout4,
            profiles: vec![profile("Body.Test")],
        })
        .expect("encode");
        let mut corrupt = bytes.clone();
        *corrupt.last_mut().expect("nonempty") ^= 1;
        assert!(decode(&corrupt).is_err());
        assert!(decode(&bytes[..bytes.len() - 1]).is_err());
        let mut trailing = bytes;
        trailing.push(0);
        assert!(decode(&trailing).is_err());
    }

    #[test]
    fn validates_endpoints_silence_carriers_and_ascii_names() {
        let mut invalid = profile("Body.Test");
        invalid.primary[0] = 0.01;
        assert!(invalid.validate().is_err());
        let mut invalid = profile("Body.Test");
        invalid.primary = [0.0; KNOTS];
        invalid.secondary = [0.0; KNOTS];
        assert!(invalid.validate().is_err());
        let mut invalid = profile("Body.Test");
        invalid.second_frequency = 0.0;
        assert!(invalid.validate().is_err());
        let invalid = profile("Body.É");
        assert!(invalid.validate().is_err());
    }

    #[test]
    fn rejects_wrong_game_tag_and_nonfinite_values() {
        let bytes = encode(&Bank {
            game: Game::SkyrimLe,
            profiles: vec![profile("Body.Test")],
        })
        .expect("encode");
        let mut wrong_tag = bytes;
        wrong_tag[8..12].copy_from_slice(&44_u32.to_le_bytes());
        let payload_crc = u32::from_le_bytes(wrong_tag[20..24].try_into().expect("crc"));
        assert_ne!(payload_crc, 0);
        assert!(decode(&wrong_tag).is_err());
        let mut invalid = profile("Body.Test");
        invalid.primary[1] = f32::NAN;
        assert!(invalid.validate().is_err());
    }

    #[test]
    fn shared_v1_binary_fixtures_have_identical_ordering_rules() {
        let sorted_bytes =
            include_bytes!("../../../tests/fixtures/audio-haptics/sorted.audiohaptics");
        let descending_bytes =
            include_bytes!("../../../tests/fixtures/audio-haptics/descending.audiohaptics");
        let duplicate_bytes =
            include_bytes!("../../../tests/fixtures/audio-haptics/duplicate.audiohaptics");
        for bytes in [sorted_bytes, descending_bytes, duplicate_bytes] {
            assert_eq!(
                read_u32(bytes, 20).expect("fixture checksum field"),
                crc32(&bytes[HEADER_BYTES..]),
                "the shared fixture must have a valid payload checksum"
            );
        }

        let sorted = decode(sorted_bytes).expect("shared sorted v1 fixture");
        assert_eq!(sorted.game, Game::SkyrimSe);
        assert_eq!(
            sorted
                .profiles
                .iter()
                .map(|profile| profile.section.as_str())
                .collect::<Vec<_>>(),
            ["Body.A", "Body.B"]
        );
        assert_eq!(sorted.profiles[0].frequency, 78.0);
        assert_eq!(sorted.profiles[1].frequency, 188.0);
        assert!(decode(descending_bytes).is_err());
        assert!(decode(duplicate_bytes).is_err());
    }
}
