use crate::MAX_RESOURCE_BYTES;
use crate::manifest::{DEFAULT_ONSET_SEARCH_MS, MAX_ONSET_SEARCH_MS};
use anyhow::{Context, Result, ensure};
use std::fs;
use std::io::{self, Write};
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};
use tempfile::tempdir;

const DECODE_TIMEOUT: Duration = Duration::from_secs(10);

#[derive(Debug)]
pub struct FfmpegDecoder {
    executable: std::path::PathBuf,
}

impl FfmpegDecoder {
    /// Creates a decoder around a caller-supplied `FFmpeg` executable.
    ///
    /// # Errors
    /// Returns an error if the executable cannot be resolved or is not a file.
    pub fn new(executable: &Path) -> Result<Self> {
        ensure!(
            executable.is_file(),
            "FFmpeg executable not found: {}",
            executable.display()
        );
        Ok(Self {
            executable: fs::canonicalize(executable)?,
        })
    }

    /// Decodes one bounded WAV/XWM resource to finite 16 kHz mono samples.
    ///
    /// # Errors
    /// Returns an error for unsupported formats, invalid samples, `FFmpeg` failure, timeout, or output-limit violation.
    #[allow(
        clippy::too_many_lines,
        reason = "The bounded temporary-file, subprocess, deadline, and PCM validation sequence is one cohesive decoder operation."
    )]
    pub fn decode(&self, source_path: &str, source: &[u8], duration_ms: u32) -> Result<Vec<f32>> {
        self.decode_with_onset(source_path, source, duration_ms, DEFAULT_ONSET_SEARCH_MS)
    }

    /// Decodes a cue with an explicitly bounded onset-search window for late-starting assets.
    ///
    /// # Errors
    /// Returns an error for invalid bounds, unsupported audio, decode failure or subprocess limits.
    #[allow(
        clippy::too_many_lines,
        reason = "Temporary input, bounded subprocess execution and PCM validation form one decoder operation."
    )]
    pub fn decode_with_onset(
        &self,
        source_path: &str,
        source: &[u8],
        duration_ms: u32,
        onset_search_ms: u32,
    ) -> Result<Vec<f32>> {
        ensure!(
            source.len() <= MAX_RESOURCE_BYTES,
            "source asset exceeds 32 MiB decode limit"
        );
        ensure!(
            duration_ms > 0 && duration_ms <= 1000,
            "decode duration must be 1..=1000 ms"
        );
        ensure!(
            (5..=MAX_ONSET_SEARCH_MS).contains(&onset_search_ms),
            "onset search must be 5..={MAX_ONSET_SEARCH_MS} ms"
        );
        let extension = Path::new(source_path)
            .extension()
            .and_then(|value| value.to_str())
            .context("resource has no audio extension")?
            .to_ascii_lowercase();
        ensure!(
            matches!(extension.as_str(), "wav" | "xwm"),
            "unsupported source type: .{extension}"
        );
        let temp = tempdir().context("create private decode directory")?;
        let input_path = temp.path().join(format!("input.{extension}"));
        let output_path = temp.path().join("decoded.f32");
        let mut input = fs::File::create(&input_path)?;
        input.write_all(source)?;
        input.sync_all()?;
        let output_cap =
            u64::from(duration_ms + onset_search_ms) * u64::from(crate::dsp::SAMPLE_RATE_HZ) * 4
                / 1000
                + 4096;
        let output_duration = format!(
            "{}.{:03}",
            (duration_ms + onset_search_ms) / 1000,
            (duration_ms + onset_search_ms) % 1000
        );
        let mut command = Command::new(&self.executable);
        command.args([
            "-y",
            "-nostdin",
            "-hide_banner",
            "-loglevel",
            "error",
            "-format_whitelist",
            "wav,xwma",
            "-protocol_whitelist",
            "file",
            "-i",
            input_path.to_string_lossy().as_ref(),
            "-map",
            "0:a:0",
            "-vn",
            "-sn",
            "-dn",
            "-t",
            &output_duration,
            "-ac",
            "1",
            "-ar",
            "16000",
            "-threads",
            "1",
            "-f",
            "f32le",
            "-acodec",
            "pcm_f32le",
        ]);
        command
            .arg(&output_path)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        no_window(&mut command);
        let mut child = command
            .spawn()
            .with_context(|| format!("launch FFmpeg {}", self.executable.display()))?;
        let exit = wait_bounded(&mut child, &output_path, output_cap)?;
        ensure!(
            exit.success(),
            "FFmpeg failed decoding {source_path} with exit status {exit}"
        );
        let output_metadata =
            fs::metadata(&output_path).context("FFmpeg produced no PCM output")?;
        ensure!(
            output_metadata.len() <= output_cap,
            "FFmpeg PCM output exceeded duration cap"
        );
        ensure!(
            output_metadata.len() % 4 == 0,
            "FFmpeg returned truncated float PCM"
        );
        let bytes = fs::read(&output_path)?;
        let (chunks, remainder) = bytes.as_chunks::<4>();
        ensure!(remainder.is_empty(), "FFmpeg returned truncated float PCM");
        let samples = chunks
            .iter()
            .map(|chunk| f32::from_le_bytes(*chunk))
            .collect::<Vec<_>>();
        ensure!(
            !samples.is_empty(),
            "FFmpeg returned no samples for {source_path}"
        );
        ensure!(
            samples.iter().all(|sample| sample.is_finite()),
            "FFmpeg returned nonfinite samples"
        );
        Ok(samples)
    }
}

fn wait_bounded(
    child: &mut impl WaitableChild,
    output_path: &Path,
    output_cap: u64,
) -> Result<std::process::ExitStatus> {
    let mut cleanup = ChildCleanup {
        child,
        completed: false,
    };
    let start = Instant::now();
    loop {
        if let Some(status) = cleanup.child.try_wait()? {
            cleanup.completed = true;
            return Ok(status);
        }
        if start.elapsed() > DECODE_TIMEOUT {
            anyhow::bail!("FFmpeg exceeded the 10 second timeout");
        }
        if fs::metadata(output_path).is_ok_and(|metadata| metadata.len() > output_cap) {
            anyhow::bail!("FFmpeg exceeded the bounded PCM output size");
        }
        thread::sleep(Duration::from_millis(15));
    }
}

trait WaitableChild {
    fn try_wait(&mut self) -> io::Result<Option<std::process::ExitStatus>>;
    fn kill(&mut self) -> io::Result<()>;
    fn wait(&mut self) -> io::Result<()>;
}

impl WaitableChild for Child {
    fn try_wait(&mut self) -> io::Result<Option<std::process::ExitStatus>> {
        Child::try_wait(self)
    }

    fn kill(&mut self) -> io::Result<()> {
        Child::kill(self)
    }

    fn wait(&mut self) -> io::Result<()> {
        Child::wait(self).map(drop)
    }
}

struct ChildCleanup<'child, T: WaitableChild> {
    child: &'child mut T,
    completed: bool,
}

impl<T: WaitableChild> Drop for ChildCleanup<'_, T> {
    fn drop(&mut self) {
        if !self.completed {
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
    }
}

#[cfg(windows)]
fn no_window(command: &mut Command) {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    command.creation_flags(CREATE_NO_WINDOW);
}

#[cfg(not(windows))]
fn no_window(_command: &mut Command) {}

#[cfg(test)]
#[allow(
    clippy::expect_used,
    reason = "Subprocess tests use fail-fast setup for the current test executable path."
)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    #[test]
    fn rejects_missing_executables_and_non_audio_extensions() {
        assert!(FfmpegDecoder::new(Path::new("missing-ffmpeg-executable")).is_err());
        let existing = std::env::current_exe().expect("test binary");
        let decoder = FfmpegDecoder {
            executable: existing,
        };
        assert!(decoder.decode("readme.txt", b"not audio", 70).is_err());
        assert!(
            decoder
                .decode_with_onset("source.wav", b"not audio", 70, 2001)
                .is_err()
        );
        assert!(
            decoder
                .decode_with_onset("source.wav", b"not audio", 70, 0)
                .is_err()
        );
    }

    #[test]
    fn reaps_child_after_wait_error_without_replacing_the_original_error() {
        #[derive(Clone)]
        struct FailingChild {
            calls: Rc<RefCell<Vec<&'static str>>>,
        }

        impl WaitableChild for FailingChild {
            fn try_wait(&mut self) -> io::Result<Option<std::process::ExitStatus>> {
                self.calls.borrow_mut().push("try_wait");
                Err(io::Error::other("injected wait failure"))
            }

            fn kill(&mut self) -> io::Result<()> {
                self.calls.borrow_mut().push("kill");
                Err(io::Error::other("injected kill failure"))
            }

            fn wait(&mut self) -> io::Result<()> {
                self.calls.borrow_mut().push("wait");
                Ok(())
            }
        }

        let calls = Rc::new(RefCell::new(Vec::new()));
        let mut child = FailingChild {
            calls: Rc::clone(&calls),
        };
        let error = wait_bounded(&mut child, Path::new("unused"), 1)
            .expect_err("the injected process query fails");

        assert_eq!(error.to_string(), "injected wait failure");
        assert_eq!(*calls.borrow(), ["try_wait", "kill", "wait"]);
    }
}
