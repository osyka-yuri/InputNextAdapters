use anyhow::Result;
use clap::{ArgAction, Parser, Subcommand};
use inputnext_haptics::archive::ResourceStore;
use inputnext_haptics::model::Game;
use std::path::PathBuf;

#[derive(Debug, Parser)]
#[command(
    name = "inputnext-haptics",
    version,
    about = "Inspect game audio archives and list bounded audio resources"
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
}

fn main() {
    if let Err(error) = run() {
        eprintln!("inputnext-haptics: {error:#}");
        std::process::exit(2);
    }
}

fn run() -> Result<()> {
    let Args {
        command:
            Command::Inspect {
                game_data,
                game,
                archives,
            },
    } = Args::parse();
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
    Ok(())
}
