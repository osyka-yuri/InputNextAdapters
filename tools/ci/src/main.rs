use std::process::ExitCode;

use clap::{Parser, Subcommand};
use inputnext_adapters_ci::{process, tooling};

#[derive(Debug, Parser)]
#[command(version, about = "InputNextAdapters host toolchain management")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    #[command(subcommand)]
    Toolchain(tooling::ToolchainCommand),
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let mut runner = process::SystemRunner;
    let result = match cli.command {
        Command::Toolchain(command) => command.run(&mut runner),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("inputnext-adapters-ci: {error:#}");
            ExitCode::FAILURE
        }
    }
}
