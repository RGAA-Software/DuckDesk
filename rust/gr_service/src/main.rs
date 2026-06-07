mod app;
mod service_host;
mod service_windows;
mod websocket_server;
mod windows_actions;
mod windows_process;

use clap::Parser;

#[derive(Parser, Debug)]
struct Cli {
    #[arg(long)]
    port: Option<u16>,

    #[arg(index = 1)]
    legacy_port: Option<u16>,

    #[arg(long, default_value_t = false)]
    console: bool,
}

#[tokio::main]
async fn main() {
    let cli = Cli::parse();
    let port = cli.port.or(cli.legacy_port);
    if let Err(err) = app::run(port, cli.console).await {
        eprintln!("GammaRayService failed: {err}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cli_accepts_legacy_positional_port() {
        let cli = Cli::try_parse_from(["GammaRayService.exe", "20375"]).unwrap();
        assert_eq!(cli.port, None);
        assert_eq!(cli.legacy_port, Some(20375));
        assert!(!cli.console);
    }

    #[test]
    fn cli_accepts_named_port() {
        let cli = Cli::try_parse_from(["GammaRayService.exe", "--port", "20375"]).unwrap();
        assert_eq!(cli.port, Some(20375));
        assert_eq!(cli.legacy_port, None);
    }

    #[test]
    fn cli_named_port_can_be_combined_with_console() {
        let cli = Cli::try_parse_from(["GammaRayService.exe", "--port", "20375", "--console"]).unwrap();
        assert_eq!(cli.port, Some(20375));
        assert!(cli.console);
    }
}
