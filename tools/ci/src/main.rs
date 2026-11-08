use std::process::ExitCode;

use clap::{Parser, Subcommand};
use inputnext_adapters_ci::{native, plan, process, tooling};

#[derive(Debug, Parser)]
#[command(
    version,
    about = "InputNextAdapters build orchestration and host toolchain"
)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    #[command(subcommand)]
    Toolchain(tooling::ToolchainCommand),
    Plan(plan::PlanArgs),
    Matrix(plan::MatrixArgs),
    Native(native::NativeArgs),
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let mut runner = process::SystemRunner;
    let result = match cli.command {
        Command::Toolchain(command) => command.run(&mut runner),
        Command::Plan(command) => command.run(),
        Command::Matrix(command) => command.run(),
        Command::Native(command) => command.run(&mut runner),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("inputnext-adapters-ci: {error:#}");
            ExitCode::FAILURE
        }
    }
}
