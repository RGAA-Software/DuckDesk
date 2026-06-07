mod manager;

use clap::{Parser, Subcommand};

use crate::manager::{ServiceManager, ServiceStatus};

#[derive(Parser)]
#[command(name = "GammaRayServiceManager")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Install {
        #[arg(long)]
        service_bin: String,
    },
    Stop,
    Remove {
        #[arg(long, default_value_t = true)]
        uninstall_service: bool,
    },
    Query,
    Path,
    Shutdown {
        #[arg(long, default_value_t = false)]
        uninstall_service: bool,
        #[arg(long)]
        current_pid: u32,
    },
}

fn main() {
    let cli = Cli::parse();
    let manager = ServiceManager::new(
        "GammaRayService",
        "GammaRayService",
        "** GammaRay Service **",
    );
    let result = match cli.command {
        Command::Install { service_bin } => manager
            .install(&service_bin)
            .map(|_| "installed".to_string()),
        Command::Stop => manager.stop().map(|_| "stopped".to_string()),
        Command::Remove { uninstall_service } => manager
            .remove(uninstall_service)
            .map(|_| "removed".to_string()),
        Command::Query => manager
            .query_status()
            .map(|status| ServiceStatus::as_str(status).to_string()),
        Command::Path => manager
            .get_service_executable_path()
            .map(|path| path.unwrap_or_default()),
        Command::Shutdown {
            uninstall_service,
            current_pid,
        } => manager
            .shutdown(uninstall_service, current_pid)
            .map(|_| "shutdown".to_string()),
    };
    match result {
        Ok(value) => {
            println!("{value}");
        }
        Err(err) => {
            eprintln!("{err}");
            std::process::exit(1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cli_accepts_stop_command() {
        let cli = Cli::try_parse_from(["GammaRayServiceManager.exe", "stop"]).unwrap();
        assert!(matches!(cli.command, Command::Stop));
    }
}
