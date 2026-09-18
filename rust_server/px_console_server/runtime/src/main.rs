use px_console_runtime::{ConsoleLaunch, ConsoleLaunchConfig, ConsoleRuntime};
use std::{io, time::Duration};
use tokio_util::sync::CancellationToken;

#[tokio::main]
async fn main() {
    if let Err(error) = run().await {
        eprintln!("Console startup/runtime failed: {error}");
        std::process::exit(1);
    }
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let ConsoleLaunch {
        database,
        deployment,
        listen,
        static_directory,
        tls,
        policy,
        vault,
        guests,
        recording_cache_root,
        recording_cache_options,
    } = ConsoleLaunchConfig::from_env()?.load().await?;
    let runtime = ConsoleRuntime::activate_with_cache(
        &database,
        deployment,
        vault,
        policy,
        guests,
        recording_cache_root,
        recording_cache_options,
    )
    .await?;
    let cancellation = runtime.cancellation_token();
    let application = runtime.product_router(static_directory);
    let listener = std::net::TcpListener::bind(listen)?;
    listener.set_nonblocking(true)?;
    let address = listener.local_addr()?;

    let server_result = if let Some((certificate, private_key)) = tls {
        let _ = rustls::crypto::ring::default_provider().install_default();
        let tls_configuration =
            axum_server::tls_rustls::RustlsConfig::from_pem_file(certificate, private_key).await?;
        let server_handle = axum_server::Handle::new();
        let shutdown_handle = server_handle.clone();
        let shutdown_cancellation = cancellation.clone();
        let shutdown_task = tokio::spawn(async move {
            wait_for_shutdown(shutdown_cancellation).await;
            shutdown_handle.graceful_shutdown(Some(Duration::from_secs(10)));
        });
        println!("Console listening https://{address}");
        let result = axum_server::from_tcp_rustls(listener, tls_configuration)?
            .handle(server_handle)
            .serve(application.into_make_service_with_connect_info::<std::net::SocketAddr>())
            .await;
        shutdown_task.abort();
        result
    } else {
        println!("Console listening http://{address} (explicit loopback development)");
        axum::serve(
            tokio::net::TcpListener::from_std(listener)?,
            application.into_make_service_with_connect_info::<std::net::SocketAddr>(),
        )
        .with_graceful_shutdown(wait_for_shutdown(cancellation.clone()))
        .await
    };
    let authority_lost = cancellation.is_cancelled();
    runtime.shutdown().await;
    server_result?;
    if authority_lost {
        return Err(io::Error::other("Console database authority was lost").into());
    }
    Ok(())
}

async fn wait_for_shutdown(cancellation: CancellationToken) {
    tokio::select! {
        _ = cancellation.cancelled() => {}
        _ = tokio::signal::ctrl_c() => {}
    }
}
