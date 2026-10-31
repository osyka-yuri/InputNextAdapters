use anyhow::{Context, Result, ensure};
use clap::{ArgAction, Parser, Subcommand};
use inputnext_haptics::archive::ResourceStore;
use inputnext_haptics::bake;
use inputnext_haptics::decoder::FfmpegDecoder;
use inputnext_haptics::manifest::Manifest;
use inputnext_haptics::model::Game;
use std::path::PathBuf;

#[derive(Debug, Parser)]
#[command(
    name = "inputnext-haptics",
    version,
    about = "Inspect game audio archives and bake bounded haptics banks offline"
)]
struct Args {
    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// List WAV and XWM resources from loose Data files and sound archives.
    Inspect {
        #[arg(long)]
        game_data: PathBuf,
        #[arg(long)]
        game: Game,
        /// Additional archives are applied in the order provided; later archives override earlier ones.
        #[arg(long = "archive", action = ArgAction::Append)]
        archives: Vec<PathBuf>,
    },
    /// Analyze selected game audio and write a versioned haptics bank plus JSON report.
    Bake {
        #[arg(long)]
        game_data: PathBuf,
        #[arg(long)]
        game: Game,
        #[arg(long)]
        manifest: PathBuf,
        #[arg(long)]
        ffmpeg: PathBuf,
        #[arg(long)]
        output: PathBuf,
        /// Emit successfully baked profiles and document missing or failed inputs.
        #[arg(long, default_value_t = false)]
        allow_missing: bool,
        #[arg(long = "archive", action = ArgAction::Append)]
        archives: Vec<PathBuf>,
    },
}

fn main() {
    if let Err(error) = run() {
        eprintln!("inputnext-haptics: {error:#}");
        std::process::exit(2);
    }
}

fn run() -> Result<()> {
    let args = Args::parse();
    match args.command {
        Command::Inspect {
            game_data,
            game,
            archives,
        } => {
            let store = ResourceStore::open(&game_data, game, &archives)?;
            let resources = store.resources().cloned().collect::<Vec<_>>();
            let document = serde_json::json!({
                "game": game.to_string(),
                "game_data": store.data_root().to_string_lossy(),
                "archive_override_order": archives,
                "resource_count": resources.len(),
                "resources": resources,
            });
            println!("{}", serde_json::to_string_pretty(&document)?);
        }
        Command::Bake {
            game_data,
            game,
            manifest: manifest_path,
            ffmpeg,
            output,
            allow_missing,
            archives,
        } => {
            let manifest = Manifest::read(&manifest_path)
                .with_context(|| format!("read manifest {}", manifest_path.display()))?;
            ensure!(
                manifest.game == game,
                "manifest game {} does not match --game {game}",
                manifest.game
            );
            let store = ResourceStore::open(&game_data, game, &archives)?;
            let decoder = FfmpegDecoder::new(&ffmpeg)?;
            let outcome = bake::bake(
                &store,
                &manifest,
                &decoder,
                &ffmpeg,
                &manifest_path,
                &output,
                allow_missing,
            )?;
            let report = serde_json::to_string_pretty(&outcome.report)?;
            println!("{report}");
            eprintln!(
                "wrote {} profiles to {}",
                outcome.bank.profiles.len(),
                outcome.bank_path.display()
            );
            eprintln!("coverage report: {}", outcome.report_path.display());
        }
    }
    Ok(())
}
