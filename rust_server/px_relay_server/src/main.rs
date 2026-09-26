use px_relay_server::{
    config::RelayConfig,
    control,
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
    tokio::select! {
        _ = tokio::signal::ctrl_c() => {}
        _ = stop_token.cancelled() => {}
        _ = cancellation.cancelled() => {}
    }
    cancellation.cancel();
}
