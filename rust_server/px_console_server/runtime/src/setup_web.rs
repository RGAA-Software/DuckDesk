use crate::{
    activate_single_server_relay, initialize_single_server, SingleServerLayout,
    SingleServerSetupInput,
};
use axum::{
    extract::{DefaultBodyLimit, State},
    http::{header, HeaderMap, StatusCode},
    response::{Html, IntoResponse},
    routing::{get, post},
    Json, Router,
};
use reqwest::Certificate;
use serde_json::{json, Value};
#[cfg(windows)]
use std::process::Command;
use std::{io::Write, sync::Arc, time::Duration};
use tokio::sync::Mutex;
use tokio_util::sync::CancellationToken;
use zeroize::Zeroizing;

const SETUP_PAGE: &str = include_str!("../../../../setup/single_server_setup.html");

struct SetupState {
    layout: SingleServerLayout,
    #[cfg(windows)]
    expected_manifest_sha256: String,
    submitted: Mutex<bool>,
    shutdown: CancellationToken,
}

pub async fn run_single_server_setup(
    layout: SingleServerLayout,
    expected_manifest_sha256: String,
    stop: CancellationToken,
) -> Result<(), Box<dyn std::error::Error>> {
    if layout.config_root.join("setup.complete").is_file() {
        return Ok(());
    }
    if !layout.linux_container
        && (expected_manifest_sha256.len() != 64
            || !expected_manifest_sha256
                .bytes()
                .all(|character| character.is_ascii_digit() || (b'a'..=b'f').contains(&character)))
    {
        return Err("invalid package manifest hash".into());
    }
    let state = Arc::new(SetupState {
        layout,
        #[cfg(windows)]
        expected_manifest_sha256,
        submitted: Mutex::new(false),
        shutdown: CancellationToken::new(),
    });
    let router = Router::new()
        .route("/", get(setup_page))
        .route("/initialize", post(initialize))
        .layer(DefaultBodyLimit::max(65_536))
        .with_state(state.clone());
    let listen_address = if state.layout.linux_container {
        "0.0.0.0:4700"
    } else {
        "127.0.0.1:4700"
    };
    let listener = tokio::net::TcpListener::bind(listen_address).await?;
    println!("Pixels Server setup: http://127.0.0.1:4700/");
    axum::serve(listener, router)
        .with_graceful_shutdown(async move {
            tokio::select! {
                _ = state.shutdown.cancelled() => {}
                _ = stop.cancelled() => {}
                _ = tokio::signal::ctrl_c() => {}
            }
        })
        .await?;
    Ok(())
}

async fn setup_page(headers: HeaderMap) -> impl IntoResponse {
    if !local_request(&headers, false) {
        return (StatusCode::FORBIDDEN, Html(""));
    }
    (StatusCode::OK, Html(SETUP_PAGE))
}

#[axum::debug_handler(state = Arc<SetupState>)]
async fn initialize(
    State(state): State<Arc<SetupState>>,
    headers: HeaderMap,
    Json(input): Json<SingleServerSetupInput>,
) -> impl IntoResponse {
    if !local_request(&headers, true) {
        return (
            StatusCode::FORBIDDEN,
            Json(json!({"error":"local_origin_required"})),
        );
    }
    {
        let mut submitted = state.submitted.lock().await;
        if *submitted || state.layout.config_root.join("setup.complete").exists() {
            return (
                StatusCode::CONFLICT,
                Json(json!({"error":"already_initialized"})),
            );
        }
        *submitted = true;
    }
    let initial_username = input.initial_username.clone();
    let initial_password = Zeroizing::new(input.initial_password.clone());
    let result = if state.layout.config_root.join("console.env").is_file() {
        existing_setup_identity(&state.layout)
    } else {
        let layout = state.layout.clone();
        tokio::task::spawn_blocking(move || {
            let runtime = tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build()
                .map_err(|_| "setup runtime unavailable".to_owned())?;
            runtime
                .block_on(initialize_single_server(input, &layout))
                .map(|setup_result| {
                    (
                        setup_result.deployment_id.to_string(),
                        setup_result.console_origin,
                    )
                })
                .map_err(|error| error.to_string())
        })
        .await
        .map_err(|_| "setup task failed".to_owned())
        .and_then(|result| result)
    };
    let (deployment_id, console_origin) = match result {
        Ok(identity) => identity,
        Err(error) => {
            *state.submitted.lock().await = false;
            return (
                StatusCode::BAD_REQUEST,
                Json(json!({"error":error.to_string()})),
            );
        }
    };
    if !state.layout.linux_container && install_windows_services(&state).await.is_err() {
        *state.submitted.lock().await = false;
        return (
            StatusCode::SERVICE_UNAVAILABLE,
            Json(json!({"error":"Windows services did not start; configuration was retained"})),
        );
    }
    let pending_token = state.layout.config_root.join("relay-registration.token");
    let registration = if pending_token.is_file() {
        px_private_files::private::read_private(&pending_token)
            .map_err(|_| "Pending Relay token is unavailable")
            .and_then(|mut bytes| {
                String::from_utf8(std::mem::take(&mut *bytes))
                    .map_err(|_| "Pending Relay token is invalid")
            })
            .map(Zeroizing::new)
    } else {
        register_relay(
            &state.layout,
            &console_origin,
            &initial_username,
            &initial_password,
        )
        .await
        .and_then(|token| {
            store_pending_token(&pending_token, token.as_bytes())?;
            Ok(token)
        })
    };
    let registered_token = match registration {
        Ok(token) => token,
        Err(_) => {
            *state.submitted.lock().await = false;
            return (
                StatusCode::SERVICE_UNAVAILABLE,
                Json(
                    json!({"error":"Console started but Relay registration failed; configuration was retained"}),
                ),
            );
        }
    };
    if activate_single_server_relay(&state.layout, &registered_token).is_err()
        || (!state.layout.linux_container && install_windows_services(&state).await.is_err())
    {
        *state.submitted.lock().await = false;
        return (
            StatusCode::SERVICE_UNAVAILABLE,
            Json(json!({"error":"Relay activation failed; configuration was retained"})),
        );
    }
    let _ = std::fs::remove_file(&pending_token);
    if std::fs::write(state.layout.config_root.join("setup.complete"), b"ready\n").is_err() {
        *state.submitted.lock().await = false;
        return (
            StatusCode::SERVICE_UNAVAILABLE,
            Json(json!({"error":"Setup completed but the completion marker was unavailable"})),
        );
    }
    let shutdown = state.shutdown.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_secs(5)).await;
        shutdown.cancel();
    });
    (
        StatusCode::OK,
        Json(json!({"deployment_id":deployment_id,"console_origin":console_origin})),
    )
}

fn store_pending_token(path: &std::path::Path, token: &[u8]) -> Result<(), &'static str> {
    let mut options = std::fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options
        .open(path)
        .map_err(|_| "Pending Relay token cannot be stored")?;
    file.write_all(token)
        .map_err(|_| "Pending Relay token cannot be stored")?;
    file.sync_all()
        .map_err(|_| "Pending Relay token cannot be stored")?;
    Ok(())
}

fn existing_setup_identity(layout: &SingleServerLayout) -> Result<(String, String), String> {
    let contents = std::fs::read_to_string(layout.config_root.join("console.env"))
        .map_err(|_| "Existing Console configuration is unavailable".to_owned())?;
    let field = |name: &str| {
        let prefix = format!("{name}=");
        let mut values = contents
            .lines()
            .filter_map(|line| line.strip_prefix(&prefix));
        match (values.next(), values.next()) {
            (Some(value), None) if !value.is_empty() => Ok(value.to_owned()),
            _ => Err("Existing Console identity is invalid".to_owned()),
        }
    };
    Ok((
        field("PIXELS_DEPLOYMENT_ID")?,
        field("PIXELS_CONSOLE_PUBLIC_ORIGIN")?,
    ))
}

fn local_request(headers: &HeaderMap, require_origin: bool) -> bool {
    if headers.contains_key("forwarded")
        || headers
            .keys()
            .any(|name| name.as_str().starts_with("x-forwarded-"))
    {
        return false;
    }
    let host = headers
        .get(header::HOST)
        .and_then(|value| value.to_str().ok());
    let expected_origin = match host {
        Some("127.0.0.1:4700") => "http://127.0.0.1:4700",
        Some("localhost:4700") => "http://localhost:4700",
        _ => return false,
    };
    !require_origin
        || headers
            .get(header::ORIGIN)
            .and_then(|value| value.to_str().ok())
            == Some(expected_origin)
}

#[cfg(windows)]
async fn install_windows_services(state: &SetupState) -> Result<(), &'static str> {
    let package_root = state.layout.package_root.clone();
    let config_root = state.layout.config_root.clone();
    let data_root = state.layout.data_root.clone();
    let install_root = state
        .layout
        .runtime_root
        .parent()
        .ok_or("invalid Windows install root")?
        .to_path_buf();
    let expected_hash = state.expected_manifest_sha256.clone();
    tokio::task::spawn_blocking(move || {
        let status = Command::new("powershell.exe")
            .args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"])
            .arg(package_root.join("install.ps1"))
            .arg("-PackageRoot")
            .arg(&package_root)
            .arg("-ExpectedManifestSha256")
            .arg(expected_hash)
            .arg("-ConfigRoot")
            .arg(config_root)
            .arg("-DataRoot")
            .arg(data_root)
            .arg("-InstallRoot")
            .arg(install_root)
            .status()
            .map_err(|_| "Windows service installer unavailable")?;
        if status.success() {
            Ok(())
        } else {
            Err("Windows service installer rejected configuration")
        }
    })
    .await
    .map_err(|_| "Windows service installer task failed")?
}

#[cfg(unix)]
async fn install_windows_services(_state: &SetupState) -> Result<(), &'static str> {
    Err("Windows service installation is unavailable on Linux")
}

async fn register_relay(
    layout: &SingleServerLayout,
    origin: &str,
    username: &str,
    password: &str,
) -> Result<Zeroizing<String>, &'static str> {
    let certificate_bytes = std::fs::read(layout.config_root.join("console-ca.crt"))
        .map_err(|_| "Console TLS root unavailable")?;
    let certificate =
        Certificate::from_pem(&certificate_bytes).map_err(|_| "Console TLS root invalid")?;
    let client = reqwest::Client::builder()
        .add_root_certificate(certificate)
        .timeout(Duration::from_secs(5))
        .build()
        .map_err(|_| "Console setup client unavailable")?;
    let host = if layout.linux_container {
        "console"
    } else {
        "localhost"
    };
    let console_base = format!("https://{host}:4600");
    let mut session_response = None;
    for _ in 0..30 {
        if let Ok(response) = client
            .post(format!("{console_base}/api/console/sessions"))
            .header("origin", origin)
            .header("x-pixels-client-type", "admin_web")
            .json(&json!({"username":username,"password":password}))
            .send()
            .await
        {
            session_response = Some(response);
            break;
        }
        tokio::time::sleep(Duration::from_secs(2)).await;
    }
    let session_response = session_response.ok_or("Console did not become ready")?;
    if !session_response.status().is_success() {
        return Err("Console administrator login rejected");
    }
    let session: Value = session_response
        .json()
        .await
        .map_err(|_| "Console administrator session invalid")?;
    let token = session["token"]
        .as_str()
        .ok_or("Console administrator token missing")?;
    let public_host = url::Url::parse(origin)
        .ok()
        .and_then(|parsed| parsed.host_str().map(str::to_owned))
        .ok_or("Console origin invalid")?;
    let created_response = client
        .post(format!("{console_base}/api/console/managed/relays"))
        .header("origin", origin)
        .header("x-pixels-client-type", "admin_web")
        .bearer_auth(token)
        .json(&json!({"name":"Single Server Relay","public_host":public_host,"public_port":4605}))
        .send()
        .await
        .map_err(|_| "Relay registration request failed")?;
    if !created_response.status().is_success() {
        return Err("Relay registration rejected");
    }
    let created: Value = created_response
        .json()
        .await
        .map_err(|_| "Relay registration response invalid")?;
    let relay_token = created["relay_token"]
        .as_str()
        .ok_or("Relay token missing")?;
    Ok(Zeroizing::new(relay_token.to_owned()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn setup_rejects_remote_hosts_and_cross_origin_posts() {
        let mut headers = HeaderMap::new();
        headers.insert(header::HOST, "127.0.0.1:4700".parse().unwrap());
        assert!(local_request(&headers, false));
        assert!(!local_request(&headers, true));
        headers.insert(header::ORIGIN, "http://127.0.0.1:4700".parse().unwrap());
        assert!(local_request(&headers, true));
        headers.insert("x-forwarded-host", "remote.example".parse().unwrap());
        assert!(!local_request(&headers, true));
        headers.remove("x-forwarded-host");
        headers.insert(header::HOST, "remote.example:4700".parse().unwrap());
        assert!(!local_request(&headers, true));
    }
}
