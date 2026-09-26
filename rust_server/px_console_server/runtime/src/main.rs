use px_console_runtime::{ConsoleLaunch, ConsoleLaunchConfig, ConsoleRuntime, RuntimeResources};
use std::{io, time::Duration};
use tokio_util::sync::CancellationToken;

#[tokio::main]
async fn main() {
    if std::env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("--wait-env-file")) {
        let Some(configuration_path) = std::env::args_os().nth(2) else {
            eprintln!("Console configuration path is missing");
            std::process::exit(2);
        };
        let configuration_path = std::path::PathBuf::from(configuration_path);
        while !configuration_path.is_file() {
            tokio::time::sleep(Duration::from_secs(2)).await;
        }
        if std::env::args_os().nth(3).as_deref() == Some(std::ffi::OsStr::new("--wait-marker")) {
            let Some(marker_path) = std::env::args_os().nth(4) else {
                eprintln!("Console readiness marker path is missing");
                std::process::exit(2);
            };
            while !std::path::Path::new(&marker_path).is_file() {
                tokio::time::sleep(Duration::from_secs(2)).await;
            }
        }
        if let Err(error) = px_server_service::load_environment_file(&configuration_path) {
            eprintln!("Console configuration failed: {error}");
            std::process::exit(2);
        }
    }
    #[cfg(windows)]
    if std::env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("--service")) {
        let Some(configuration_path) = std::env::args_os().nth(2) else {
            eprintln!("Console service configuration path is missing");
            std::process::exit(2);
        };
        if let Err(error) =
            px_server_service::load_environment_file(std::path::Path::new(&configuration_path))
                .and_then(|_| px_server_service::dispatch("Pixels.Console", run_windows_service))
        {
            eprintln!("Console Windows service failed: {error}");
            std::process::exit(1);
        }
        return;
    }
    if let Err(error) = run(CancellationToken::new()).await {
        eprintln!("Console startup/runtime failed: {error}");
        std::process::exit(1);
    }
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
        relay_admission,
        release,
        license,
        license_config,
    } = ConsoleLaunchConfig::from_env()?.load().await?;
    let runtime = ConsoleRuntime::activate_product_with_cache_optional(
        &database,
        deployment,
        vault,
        policy,
        guests,
        RuntimeResources {
            recording_cache: Some((recording_cache_root, recording_cache_options)),
            relay_admission,
            release,
        },
        license,
        license_config,
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
            wait_for_shutdown(shutdown_cancellation, stop_token.clone()).await;
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
        .with_graceful_shutdown(wait_for_shutdown(cancellation.clone(), stop_token))
        .await
    };
    let authority_lost = cancellation.is_cancelled();
    runtime.shutdown().await;
    server_result?;
    if authority_lost {
        return Err(io::Error::other("Console runtime authority was lost").into());
    }
    Ok(())
}

#[cfg(windows)]
async fn wait_for_shutdown(cancellation: CancellationToken, stop_token: CancellationToken) {
    tokio::select! {
        _ = cancellation.cancelled() => {}
        _ = stop_token.cancelled() => {}
        _ = tokio::signal::ctrl_c() => {}
    }
}

#[cfg(unix)]
async fn wait_for_shutdown(cancellation: CancellationToken, stop_token: CancellationToken) {
    use tokio::signal::unix::{signal, SignalKind};

    let Ok(mut terminate) = signal(SignalKind::terminate()) else {
        cancellation.cancel();
        return;
    };
    tokio::select! {
        _ = cancellation.cancelled() => {}
        _ = stop_token.cancelled() => {}
        _ = tokio::signal::ctrl_c() => {}
        _ = terminate.recv() => {}
    }
}
