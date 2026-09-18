#![cfg_attr(not(test), windows_subsystem = "windows")]

mod app;
mod node_control_client;
mod node_control_store;
mod node_telemetry;
mod parsec_vdd;
mod product_descriptor;
mod service_host;
mod service_windows;
mod user_proxy;
mod virtual_display_manager;
mod virtual_display_session;
mod virtual_display_store;
mod websocket_server;
mod windows_actions;
mod windows_process;

use clap::{Parser, ValueEnum};

#[derive(ValueEnum, Debug, Clone, Copy, PartialEq, Eq)]
enum VirtualDisplayCliOperation {
    Query,
    Create,
    RemoveLast,
    ResetOwned,
}

#[derive(ValueEnum, Debug, Clone, Copy, PartialEq, Eq)]
enum VirtualDisplaySessionWorkerOperation {
    Query,
    Create,
    RemoveLast,
}

#[derive(Parser, Debug)]
struct Cli {
    #[arg(long)]
    port: Option<u16>,

    #[arg(long, default_value_t = false)]
    console: bool,

    /// Read a strict node-control configuration from stdin and protect it for this machine.
    #[arg(long, default_value_t = false, conflicts_with_all = ["clear_node_control", "port", "console", "virtual_display", "virtual_display_session_worker"])]
    configure_node_control: bool,

    /// Remove this machine's protected node-control configuration.
    #[arg(long, default_value_t = false, conflicts_with_all = ["configure_node_control", "port", "console", "virtual_display", "virtual_display_session_worker"])]
    clear_node_control: bool,

    /// Local administrator diagnostics for the Service-owned virtual display.
    #[arg(long, value_enum)]
    virtual_display: Option<VirtualDisplayCliOperation>,

    #[arg(long, default_value_t = virtual_display_manager::DEFAULT_WIDTH)]
    virtual_display_width: u32,

    #[arg(long, default_value_t = virtual_display_manager::DEFAULT_HEIGHT)]
    virtual_display_height: u32,

    #[arg(long, default_value_t = virtual_display_manager::DEFAULT_REFRESH_HZ)]
    virtual_display_refresh_hz: u32,

    /// Internal Session-0 bridge. Not a public administration interface.
    #[arg(long, value_enum, hide = true)]
    virtual_display_session_worker: Option<VirtualDisplaySessionWorkerOperation>,

    #[arg(long, hide = true)]
    virtual_display_worker_result: Option<std::path::PathBuf>,

    #[arg(long, hide = true)]
    virtual_display_worker_nonce: Option<String>,
}

#[tokio::main]
async fn main() {
    let cli = Cli::parse();
    if cli.configure_node_control {
        if let Err(error) = node_control_store::configure_from_stdin() {
            eprintln!("node-control configuration failed: {error}");
            std::process::exit(2);
        }
        println!("node-control configuration stored; restart Pixels Service to apply it");
        return;
    }
    if cli.clear_node_control {
        if let Err(error) = node_control_store::clear_installed_configuration() {
            eprintln!("node-control configuration removal failed: {error}");
            std::process::exit(2);
        }
        println!("node-control configuration removed; restart Pixels Service to apply it");
        return;
    }
    if let Some(operation) = cli.virtual_display_session_worker {
        let result_file = cli
            .virtual_display_worker_result
            .unwrap_or_else(|| std::process::exit(3));
        let nonce = cli
            .virtual_display_worker_nonce
            .unwrap_or_else(|| std::process::exit(3));
        if app::run_virtual_display_session_worker(
            operation,
            cli.virtual_display_width,
            cli.virtual_display_height,
            cli.virtual_display_refresh_hz,
            &result_file,
            &nonce,
        )
        .is_err()
        {
            std::process::exit(3);
        }
        return;
    }
    if let Some(operation) = cli.virtual_display {
        if let Err(err) = app::run_virtual_display_command(
            operation,
            cli.virtual_display_width,
            cli.virtual_display_height,
            cli.virtual_display_refresh_hz,
        ) {
            eprintln!("virtual display command failed: {err}");
            std::process::exit(2);
        }
        return;
    }
    if let Err(err) = app::run(cli.port, cli.console).await {
        eprintln!("px_service failed: {err}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cli_rejects_positional_port() {
        assert!(Cli::try_parse_from(["px_service.exe", "4603"]).is_err());
    }

    #[test]
    fn cli_accepts_named_port() {
        let cli = Cli::try_parse_from(["px_service.exe", "--port", "4603"]).unwrap();
        assert_eq!(cli.port, Some(4603));
    }

    #[test]
    fn cli_named_port_can_be_combined_with_console() {
        let cli = Cli::try_parse_from(["px_service.exe", "--port", "4603", "--console"]).unwrap();
        assert_eq!(cli.port, Some(4603));
        assert!(cli.console);
    }

    #[test]
    fn cli_accepts_virtual_display_query() {
        let cli = Cli::try_parse_from(["px_service.exe", "--virtual-display", "query"]).unwrap();
        assert_eq!(cli.virtual_display, Some(VirtualDisplayCliOperation::Query));
        assert_eq!(cli.virtual_display_width, 1920);
        assert_eq!(cli.virtual_display_height, 1080);
        assert_eq!(cli.virtual_display_refresh_hz, 60);
    }
}
