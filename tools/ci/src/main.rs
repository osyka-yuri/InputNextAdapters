use std::process::ExitCode;

use clap::{Parser, Subcommand};
use inputnext_adapters_ci::{native, package, plan, process, qualification, tooling};

#[derive(Debug, Parser)]
#[command(
    version,
    about = "InputNextAdapters native build and runtime qualification"
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
    Package(package::PackageArgs),
    Qualify(qualification::QualifyArgs),
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let mut runner = process::SystemRunner;
    let result = match cli.command {
        Command::Toolchain(command) => command.run(&mut runner),
        Command::Plan(command) => command.run(),
        Command::Matrix(command) => command.run(),
        Command::Native(command) => command.run(&mut runner),
        Command::Package(command) => command.run(&mut runner),
        Command::Qualify(command) => command.run(&mut runner),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("inputnext-adapters-ci: {error:#}");
            ExitCode::FAILURE
        }
    }
}
