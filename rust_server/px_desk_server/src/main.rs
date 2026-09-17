use px_desk_server::{config::Settings, router, AppState};
use std::{sync::Arc, time::Duration};

#[tokio::main]
async fn main() {
    if let Err(error) = run().await {
        eprintln!("Desk startup/runtime failed: {error}");
        std::process::exit(1);
    }
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let settings = Settings::from_env()?;
    let state = Arc::new(AppState::connect(&settings).await?);
    let app = router(state.clone(), &settings.static_directory);
    let listener = std::net::TcpListener::bind(settings.listen)?;
    listener.set_nonblocking(true)?;
    let address = listener.local_addr()?;
    if let Some((cert, key)) = &settings.tls {
        let _ = rustls::crypto::ring::default_provider().install_default();
        let tls = axum_server::tls_rustls::RustlsConfig::from_pem_file(cert, key).await?;
        let handle = axum_server::Handle::new();
        let shutdown = handle.clone();
        let signal_task = tokio::spawn(async move {
            let _ = tokio::signal::ctrl_c().await;
            shutdown.graceful_shutdown(Some(Duration::from_secs(10)));
        });
        println!("Desk listening https://{address}");
        let result = axum_server::from_tcp_rustls(listener, tls)?
            .handle(handle)
            .serve(app.into_make_service())
            .await;
        signal_task.abort();
        result?;
    } else {
        println!("Desk listening http://{address} (explicit loopback development)");
        axum::serve(tokio::net::TcpListener::from_std(listener)?, app)
            .with_graceful_shutdown(async {
                let _ = tokio::signal::ctrl_c().await;
            })
            .await?;
    }
    state.close().await;
    Ok(())
}
