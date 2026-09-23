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
        .with_graceful_shutdown(shutdown_signal(cancellation.clone()))
        .await?;
    cancellation.cancel();
    let _ = control_task.await;
    Ok(())
}

async fn shutdown_signal(cancellation: CancellationToken) {
    let _ = tokio::signal::ctrl_c().await;
    cancellation.cancel();
}
