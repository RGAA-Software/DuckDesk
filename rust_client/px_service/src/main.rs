#![cfg_attr(not(test), windows_subsystem = "windows")]

mod app;
mod rdp_host_setup;
mod hardware_probe;
mod hardware_probe_process;
mod iroh_endpoints;
mod node_control_client;
mod node_control_store;
mod node_gpu_runtime_binding;
mod node_gpu_telemetry;
mod node_hardware_telemetry;
mod node_telemetry;
mod parsec_vdd;
mod product_descriptor;
mod recording_inventory;
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
    /// Internal isolated native-driver worker; not a service or configuration mode.
    #[arg(long, value_enum, hide = true, conflicts_with_all = ["port", "console", "configure_node_control", "clear_node_control", "virtual_display", "virtual_display_session_worker"])]
    hardware_probe: Option<hardware_probe::ProbeKind>,

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

fn main() {
    let cli = Cli::parse();
    if let Some(kind) = cli.hardware_probe {
        if let Err(error) = hardware_probe::run_worker(kind) {
            eprintln!("hardware probe {} failed: {error}", kind.name());
            std::process::exit(2);
        }
        return;
    }
    run_service(cli);
}

#[tokio::main]
async fn run_service(cli: Cli) {
    if rustls::crypto::ring::default_provider()
        .install_default()
        .is_err()
    {
        eprintln!("px_service failed: cannot install the process TLS crypto provider");
        std::process::exit(5);
    }
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
    fn internal_hardware_workers_have_typed_modes_and_cannot_start_the_service() {
        for mode in ["gpu", "cpu", "memory", "disk"] {
            let cli = Cli::try_parse_from(["px_service.exe", "--hardware-probe", mode]).unwrap();
            assert_eq!(cli.hardware_probe.unwrap().name(), mode);
            assert!(
                Cli::try_parse_from(["px_service.exe", "--hardware-probe", mode, "--console"])
                    .is_err()
            );
            assert!(Cli::try_parse_from([
                "px_service.exe",
                "--hardware-probe",
                mode,
                "--port",
                "4603"
            ])
            .is_err());
        }
        assert!(Cli::try_parse_from(["px_service.exe", "--hardware-probe", "unknown"]).is_err());
    }

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
