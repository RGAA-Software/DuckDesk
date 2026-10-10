use px_relay_server::{
    config::RelayConfig,
    control,
    iroh::{IrohRelay, IrohRelayConfig},
    server::{router_with_state, RelayServerState},
};
use tokio::net::TcpListener;
use tokio_util::sync::CancellationToken;
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    if std::env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("--wait-env-file")) {
        let configuration_path = std::env::args_os()
            .nth(2)
            .ok_or("Relay configuration path is missing")?;
        let configuration_path = std::path::PathBuf::from(configuration_path);
        while !configuration_path.is_file() {
            tokio::time::sleep(std::time::Duration::from_secs(2)).await;
        }
        if std::env::args_os().nth(3).as_deref() == Some(std::ffi::OsStr::new("--wait-marker")) {
            let marker_path = std::env::args_os()
                .nth(4)
                .ok_or("Relay readiness marker path is missing")?;
            let marker_path = std::path::PathBuf::from(marker_path);
            while !marker_path.is_file() {
                tokio::time::sleep(std::time::Duration::from_secs(2)).await;
            }
        }
        px_server_service::load_environment_file(&configuration_path)?;
    }
    #[cfg(windows)]
    if std::env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("--service")) {
        let configuration_path = std::env::args_os()
            .nth(2)
            .ok_or("Relay service configuration path is missing")?;
        px_server_service::load_environment_file(std::path::Path::new(&configuration_path))?;
        px_server_service::dispatch("Pixels.Relay", run_windows_service)?;
        return Ok(());
    }
    run(CancellationToken::new()).await
}

#[cfg(windows)]
fn run_windows_service(
    runtime: &tokio::runtime::Runtime,
    stop_token: CancellationToken,
) -> Result<(), String> {
    runtime
        .block_on(run(stop_token))
        .map_err(|error| error.to_string())
}

async fn run(stop_token: CancellationToken) -> Result<(), Box<dyn std::error::Error>> {
    let _ = rustls::crypto::ring::default_provider().install_default();
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .init();
    let iroh_configuration =
        if std::env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("--iroh-config")) {
            Some(
                std::env::args_os()
                    .nth(2)
                    .ok_or("Relay iroh configuration path is missing")?,
            )
        } else {
            std::env::var_os("PIXELS_RELAY_IROH_CONFIG")
        };
    if let Some(configuration_path) = iroh_configuration {
        let config = IrohRelayConfig::load(std::path::Path::new(&configuration_path))?;
        let control_plane = if config.console_managed {
            Some(
                px_relay_server::config::ControlPlaneConfig::from_environment()
                    .map_err(std::io::Error::other)?,
            )
        } else {
            None
        };
        let relay = IrohRelay::start(&config).await?;
        let management = relay.management();
        if control_plane.is_some() {
            management.set_draining(true);
        }
        let cancellation = CancellationToken::new();
        let relay_task = async {
            let runtime_task = async {
                let result = relay.run(cancellation.clone()).await;
                cancellation.cancel();
                result
            };
            let control_task = async {
                if let Some(control_plane) = control_plane {
                    control::run_iroh(management, control_plane, cancellation.clone()).await;
                }
            };
            let (result, ()) = tokio::join!(runtime_task, control_task);
            result
        };
        tokio::pin!(relay_task);
        tokio::select! {
            result = &mut relay_task => result?,
            _ = shutdown_signal(cancellation.clone(), stop_token) => relay_task.await?,
        }
        return Ok(());
    }
    let config = RelayConfig::from_environment().map_err(std::io::Error::other)?;
    let listener = TcpListener::bind(config.listen).await?;
    tracing::info!(listen = %config.listen, "Pixels Relay started");
    let state = RelayServerState::new(config);
    let cancellation = CancellationToken::new();
    let control_task = tokio::spawn(control::run(state.clone(), cancellation.clone()));
    axum::serve(listener, router_with_state(state))
        .with_graceful_shutdown(shutdown_signal(cancellation.clone(), stop_token))
        .await?;
    cancellation.cancel();
    let _ = control_task.await;
    Ok(())
}

async fn shutdown_signal(cancellation: CancellationToken, stop_token: CancellationToken) {
    #[cfg(unix)]
    let mut terminate = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
        .expect("install Relay SIGTERM handler");
    #[cfg(unix)]
    let terminate_signal = terminate.recv();
    #[cfg(not(unix))]
    let terminate_signal = std::future::pending::<()>();
    tokio::select! {
        _ = tokio::signal::ctrl_c() => {}
        _ = terminate_signal => {}
        _ = stop_token.cancelled() => {}
        _ = cancellation.cancelled() => {}
    }
    cancellation.cancel();
}
