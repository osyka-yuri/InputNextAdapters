use crate::MAX_RESOURCE_PATH_BYTES;
use anyhow::{Context, Result, ensure};

pub(crate) const MAX_GLOB_PATTERNS: usize = 32;
pub(crate) const MAX_GLOB_PATTERN_BYTES: usize = 256;
const MAX_GLOB_WORK: usize = 64 * 1024 * 1024;
const GLOB_WORDS: usize = (MAX_GLOB_PATTERN_BYTES + 64) / 64;

#[derive(Default)]
pub(crate) struct GlobWork {
    steps: usize,
}

impl GlobWork {
    fn step(&mut self) -> Result<()> {
        self.steps = self
            .steps
            .checked_add(1)
            .context("glob matcher work counter overflow")?;
        ensure!(
            self.steps <= MAX_GLOB_WORK,
            "glob matching exceeded the {MAX_GLOB_WORK}-step work limit"
        );
        Ok(())
    }
}

#[derive(Clone, Copy)]
enum GlobToken {
    Literal(u8),
    Any,
    Star { crosses_separator: bool },
}

struct GlobPattern {
    tokens: Vec<GlobToken>,
    words: usize,
    any_mask: [u64; GLOB_WORDS],
    single_star_mask: [u64; GLOB_WORDS],
    deep_star_mask: [u64; GLOB_WORDS],
    star_mask: [u64; GLOB_WORDS],
    literal_masks: [[u64; GLOB_WORDS]; 256],
}

pub(crate) struct GlobSet {
    patterns: Vec<GlobPattern>,
    active: [u64; GLOB_WORDS],
    next: [u64; GLOB_WORDS],
}

impl GlobSet {
    pub(crate) fn parse(patterns: &[String]) -> Result<Self> {
        ensure!(
            !patterns.is_empty() && patterns.len() <= MAX_GLOB_PATTERNS,
            "glob query needs 1..={MAX_GLOB_PATTERNS} patterns"
        );
        Ok(Self {
            patterns: patterns
                .iter()
                .map(|pattern| GlobPattern::parse(pattern))
                .collect::<Result<Vec<_>>>()?,
            active: [0; GLOB_WORDS],
            next: [0; GLOB_WORDS],
        })
    }

    pub(crate) fn matches(&mut self, value: &str, work: &mut GlobWork) -> Result<bool> {
        for index in 0..self.patterns.len() {
            if self.patterns[index].matches(value, work, &mut self.active, &mut self.next)? {
                return Ok(true);
            }
        }
        Ok(false)
    }
}

impl GlobPattern {
    fn parse(pattern: &str) -> Result<Self> {
        let normalized = pattern.replace('\\', "/").to_ascii_lowercase();
        ensure!(
            !normalized.is_empty() && normalized.len() <= MAX_GLOB_PATTERN_BYTES,
            "glob pattern must contain 1..={MAX_GLOB_PATTERN_BYTES} bytes"
        );
        let bytes = normalized.as_bytes();
        let mut tokens = Vec::with_capacity(bytes.len());
        let mut cursor = 0;
        while cursor < bytes.len() {
            match bytes[cursor] {
                b'*' => {
                    let start = cursor;
                    while bytes.get(cursor) == Some(&b'*') {
                        cursor += 1;
                    }
                    tokens.push(GlobToken::Star {
                        crosses_separator: cursor - start >= 2,
                    });
                }
                b'?' => {
                    tokens.push(GlobToken::Any);
                    cursor += 1;
                }
                literal => {
                    tokens.push(GlobToken::Literal(literal));
                    cursor += 1;
                }
            }
        }
        let words = (tokens.len() + 64) / 64;
        ensure!(words <= GLOB_WORDS, "glob state count exceeds safety bound");
        let mut result = Self {
            tokens,
            words,
            any_mask: [0; GLOB_WORDS],
            single_star_mask: [0; GLOB_WORDS],
            deep_star_mask: [0; GLOB_WORDS],
            star_mask: [0; GLOB_WORDS],
            literal_masks: [[0; GLOB_WORDS]; 256],
        };
        for (index, token) in result.tokens.iter().enumerate() {
            let word = index / 64;
            let bit = 1_u64 << (index % 64);
            match token {
                GlobToken::Literal(byte) => result.literal_masks[usize::from(*byte)][word] |= bit,
                GlobToken::Any => result.any_mask[word] |= bit,
                GlobToken::Star {
                    crosses_separator: true,
                } => {
                    result.star_mask[word] |= bit;
                    result.deep_star_mask[word] |= bit;
                }
                GlobToken::Star {
                    crosses_separator: false,
                } => {
                    result.star_mask[word] |= bit;
                    result.single_star_mask[word] |= bit;
                }
            }
        }
        Ok(result)
    }

    fn matches(
        &self,
        value: &str,
        work: &mut GlobWork,
        active: &mut [u64; GLOB_WORDS],
        next: &mut [u64; GLOB_WORDS],
    ) -> Result<bool> {
        ensure!(
            value.len() <= MAX_RESOURCE_PATH_BYTES,
            "resource path exceeds {MAX_RESOURCE_PATH_BYTES}-byte glob limit"
        );
        active.fill(0);
        active[0] = 1;
        self.close_stars(active, work)?;
        // Active bit i means token i is reachable; scan a fixed word bound rather than retrying suffixes.
        for byte in value.as_bytes() {
            next.fill(0);
            let lower = usize::from(byte.to_ascii_lowercase());
            let may_cross_separator = *byte != b'/';
            let mut carry = 0_u64;
            for word in 0..self.words {
                work.step()?;
                let matching_tokens = self.literal_masks[lower][word]
                    | if may_cross_separator {
                        self.any_mask[word]
                    } else {
                        0
                    };
                let repeating_stars = self.deep_star_mask[word]
                    | if may_cross_separator {
                        self.single_star_mask[word]
                    } else {
                        0
                    };
                let current = active[word];
                let matched = current & matching_tokens;
                next[word] = (current & repeating_stars) | (matched << 1) | carry;
                carry = matched >> 63;
            }
            self.close_stars(next, work)?;
            std::mem::swap(active, next);
        }
        let accept_state = self.tokens.len();
        Ok(active[accept_state / 64] & (1_u64 << (accept_state % 64)) != 0)
    }

    fn close_stars(&self, states: &mut [u64; GLOB_WORDS], work: &mut GlobWork) -> Result<()> {
        // Parsing collapses every consecutive star run, so one epsilon shift closes all stars.
        let mut carry = 0_u64;
        for (word, state) in states.iter_mut().enumerate().take(self.words) {
            work.step()?;
            let active_stars = *state & self.star_mask[word];
            *state |= (active_stars << 1) | carry;
            carry = active_stars >> 63;
        }
        Ok(())
    }
}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    reason = "Glob tests use fail-fast setup so invalid fixtures fail at their source."
)]
mod tests {
    use super::*;

    fn all_strings(alphabet: &[u8], max_len: usize) -> Vec<Vec<u8>> {
        let mut result = vec![Vec::new()];
        let mut frontier = vec![Vec::new()];
        for _ in 0..max_len {
            let mut next = Vec::new();
            for prefix in &frontier {
                for byte in alphabet {
                    let mut value = prefix.clone();
                    value.push(*byte);
                    next.push(value);
                }
            }
            result.extend(next.iter().cloned());
            frontier = next;
        }
        result
    }

    fn reference_glob(pattern: &[u8], value: &[u8]) -> bool {
        if pattern.is_empty() {
            return value.is_empty();
        }
        if pattern[0] == b'*' {
            let deep = pattern.get(1) == Some(&b'*');
            let rest = if deep { &pattern[2..] } else { &pattern[1..] };
            for index in 0..=value.len() {
                if !deep && index > 0 && value[index - 1] == b'/' {
                    break;
                }
                if reference_glob(rest, &value[index..]) {
                    return true;
                }
            }
            return false;
        }
        if value.is_empty() {
            return false;
        }
        if pattern[0] == b'?' {
            return value[0] != b'/' && reference_glob(&pattern[1..], &value[1..]);
        }
        pattern[0].eq_ignore_ascii_case(&value[0]) && reference_glob(&pattern[1..], &value[1..])
    }

    #[test]
    fn bounded_glob_nfa_handles_quality_reproducer() {
        let pattern = format!("{}Z.wav", "*a".repeat(20));
        let value = format!("sound/{}.wav", "a".repeat(40));
        let glob = GlobPattern::parse(&pattern).expect("valid bounded pattern");
        let mut work = GlobWork::default();
        assert!(
            !glob
                .matches(
                    &value,
                    &mut work,
                    &mut [0; GLOB_WORDS],
                    &mut [0; GLOB_WORDS]
                )
                .expect("bounded match")
        );
        assert!(work.steps < 100_000, "used {} matcher steps", work.steps);
    }

    #[test]
    fn glob_semantics_match_segment_and_recursive_wildcards() {
        let patterns = all_strings(b"a/*?", 4);
        let values = all_strings(b"ab/", 3);
        let mut work = GlobWork::default();
        let mut active = [0; GLOB_WORDS];
        let mut next = [0; GLOB_WORDS];
        for pattern in patterns.iter().filter(|pattern| !pattern.is_empty()) {
            let pattern_text = String::from_utf8(pattern.clone()).expect("ASCII pattern");
            let glob = GlobPattern::parse(&pattern_text).expect("pattern under size limit");
            for value in &values {
                let value_text = String::from_utf8(value.clone()).expect("ASCII path");
                assert_eq!(
                    glob.matches(&value_text, &mut work, &mut active, &mut next)
                        .expect("glob work remains bounded"),
                    reference_glob(pattern, value),
                    "pattern {pattern_text:?}, value {value_text:?}"
                );
            }
        }

        let mixed_case = GlobPattern::parse("SOUND/*.WAV").expect("case-folded pattern");
        assert!(
            mixed_case
                .matches("sound/Pulse.wav", &mut work, &mut active, &mut next)
                .expect("case-insensitive match")
        );

        assert!(GlobPattern::parse(&"a".repeat(MAX_GLOB_PATTERN_BYTES + 1)).is_err());
        assert!(
            GlobPattern::parse("folder/*.WAV")
                .expect("normal pattern")
                .matches(
                    &"a".repeat(MAX_RESOURCE_PATH_BYTES + 1),
                    &mut work,
                    &mut active,
                    &mut next
                )
                .is_err()
        );
    }

    #[test]
    fn glob_set_enforces_pattern_count_limits() {
        assert!(GlobSet::parse(&[]).is_err());
        let too_many = vec!["*".to_owned(); MAX_GLOB_PATTERNS + 1];
        assert!(GlobSet::parse(&too_many).is_err());
    }

    #[test]
    fn glob_work_budget_fails_instead_of_returning_a_false_nonmatch() {
        let glob = GlobPattern::parse("sound/*.wav").expect("valid pattern");
        let mut work = GlobWork {
            steps: MAX_GLOB_WORK,
        };
        let error = glob
            .matches(
                "sound/pulse.wav",
                &mut work,
                &mut [0; GLOB_WORDS],
                &mut [0; GLOB_WORDS],
            )
            .expect_err("excessive work fails explicitly");
        assert!(error.to_string().contains("glob matching exceeded"));
    }
}
