use futures_util::{SinkExt, StreamExt};
use px_backup::{
    BackupControlConfig, BackupDaemonStatus, BackupToConsole, ConsoleToBackup,
    MAX_CONTROL_MESSAGE_BYTES,
};
use std::{
    fs::File,
    io::BufReader,
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::Sender,
        Arc,
    },
    time::Duration,
};
use tokio::sync::oneshot;
use tokio_tungstenite::{connect_async_tls_with_config, tungstenite::Message, Connector};
use uuid::Uuid;

pub struct ManualRequest {
    pub task_id: Uuid,
    pub result: oneshot::Sender<Result<(), &'static str>>,
}

pub fn run(
    control: BackupControlConfig,
    status_path: PathBuf,
    deployment_id: Uuid,
    manual_sender: Sender<ManualRequest>,
    stopping: Arc<AtomicBool>,
    executing: Arc<AtomicBool>,
) {
    let _ = rustls::crypto::ring::default_provider().install_default();
    let Ok(runtime) = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
    else {
        return;
    };
    runtime.block_on(async move {
        let Ok(connector) = load_connector(&control) else {
            eprintln!("px_backup: Console control CA rejected; scheduled backup remains active");
            return;
        };
        let mut outage_reported = false;
        while !stopping.load(Ordering::Acquire) {
            let connection = tokio::time::timeout(
                Duration::from_secs(5),
                connect_async_tls_with_config(
                    &control.console_url,
                    None,
                    false,
                    Some(connector.clone()),
                ),
            )
            .await;
            if let Ok(Ok((socket, _))) = connection {
                let _ = session(
                    socket,
                    &control,
                    &status_path,
                    deployment_id,
                    &manual_sender,
                    &stopping,
                    &executing,
                )
                .await;
            }
            if !outage_reported && !stopping.load(Ordering::Acquire) {
                eprintln!(
                    "px_backup: Console control disconnected; scheduled backup remains active"
                );
                outage_reported = true;
            }
            if stopping.load(Ordering::Acquire) {
                break;
            }
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    });
}

fn load_connector(control: &BackupControlConfig) -> Result<Connector, ()> {
    let certificate_file = File::open(&control.console_ca_file).map_err(|_| ())?;
    let certificates = rustls_pemfile::certs(&mut BufReader::new(certificate_file))
        .collect::<Result<Vec<_>, _>>()
        .map_err(|_| ())?;
    if certificates.is_empty() {
        return Err(());
    }
    let mut roots = rustls::RootCertStore::empty();
    let (accepted, rejected) = roots.add_parsable_certificates(certificates);
    if accepted == 0 || rejected != 0 {
        return Err(());
    }
    Ok(Connector::Rustls(Arc::new(
        rustls::ClientConfig::builder()
            .with_root_certificates(roots)
            .with_no_client_auth(),
    )))
}

async fn session(
    mut socket: tokio_tungstenite::WebSocketStream<
        tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
    >,
    control: &BackupControlConfig,
    status_path: &PathBuf,
    deployment_id: Uuid,
    manual_sender: &Sender<ManualRequest>,
    stopping: &AtomicBool,
    executing: &AtomicBool,
) -> Result<(), ()> {
    send(
        &mut socket,
        &BackupToConsole::Authenticate {
            deployment_id,
            token: control.token.clone(),
        },
    )
    .await?;
    let Some(Ok(Message::Text(authentication))) =
        tokio::time::timeout(Duration::from_secs(5), socket.next())
            .await
            .map_err(|_| ())?
    else {
        return Err(());
    };
    if !matches!(
        serde_json::from_str::<ConsoleToBackup>(&authentication),
        Ok(ConsoleToBackup::Authenticated)
    ) {
        return Err(());
    }
    let mut interval = tokio::time::interval(Duration::from_secs(5));
    interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    loop {
        tokio::select! {
            _ = interval.tick() => {
                if stopping.load(Ordering::Acquire) { return Ok(()); }
                let bytes = px_private_files::private::read_private(status_path).map_err(|_| ())?;
                if bytes.len() > MAX_CONTROL_MESSAGE_BYTES { return Err(()); }
                let status = serde_json::from_slice::<BackupDaemonStatus>(&bytes).map_err(|_| ())?;
                if status.deployment_id != deployment_id { return Err(()); }
                send(&mut socket, &BackupToConsole::Status { status }).await?;
            }
            incoming = socket.next() => {
                let Some(Ok(Message::Text(encoded))) = incoming else { return Err(()); };
                if encoded.len() > MAX_CONTROL_MESSAGE_BYTES { return Err(()); }
                let ConsoleToBackup::Trigger { task_id } = serde_json::from_str(&encoded).map_err(|_| ())? else { return Err(()); };
                if task_id.is_nil() { return Err(()); }
                let result = if executing.load(Ordering::Acquire) {
                    Err("busy")
                } else {
                    let (result_sender, result_receiver) = oneshot::channel();
                    manual_sender.send(ManualRequest { task_id, result: result_sender }).map_err(|_| ())?;
                    tokio::time::timeout(Duration::from_secs(5), result_receiver).await.map_err(|_| ())?.map_err(|_| ())?
                };
                send(&mut socket, &BackupToConsole::TriggerResult {
                    task_id,
                    accepted: result.is_ok(),
                    code: result.err().map(str::to_string),
                }).await?;
            }
        }
    }
}

async fn send(
    socket: &mut tokio_tungstenite::WebSocketStream<
        tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
    >,
    message: &BackupToConsole,
) -> Result<(), ()> {
    let encoded = serde_json::to_string(message).map_err(|_| ())?;
    if encoded.len() > MAX_CONTROL_MESSAGE_BYTES {
        return Err(());
    }
    tokio::time::timeout(
        Duration::from_secs(5),
        socket.send(Message::Text(encoded.into())),
    )
    .await
    .map_err(|_| ())?
    .map_err(|_| ())
}

#[cfg(test)]
mod tests {
    use super::*;
    use px_backup::BACKUP_DAEMON_STATUS_SCHEMA_VERSION;
    use std::fs;
    use tokio::net::TcpListener;

    #[tokio::test]
    async fn websocket_reports_status_and_acknowledges_manual_trigger() {
        let temporary_directory = tempfile::Builder::new()
            .prefix("pixels-backup-control-")
            .tempdir()
            .unwrap();
        make_private(temporary_directory.path());
        let deployment_id = Uuid::new_v4();
        let status_path = temporary_directory.path().join("status.json");
        let status = BackupDaemonStatus {
            schema_version: BACKUP_DAEMON_STATUS_SCHEMA_VERSION,
            deployment_id,
            service_started_at_unix: 1_000,
            updated_at_unix: 1_001,
            scheduler_revision: 1,
            active_task: None,
            last_success_at_unix: Some(1_000),
            last_recovery_set_id: Some(Uuid::new_v4()),
            last_local_recovery_set_id: None,
            offsite_configured: false,
            offsite_repository_healthy: false,
            last_offsite_recovery_set_id: None,
            last_failure_code: None,
            consecutive_failures: 0,
            overdue: false,
            alerts: vec![],
        };
        px_private_files::private::create_private(
            &status_path,
            &serde_json::to_vec(&status).unwrap(),
        )
        .unwrap();

        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let listen_address = listener.local_addr().unwrap();
        let manual_task_id = Uuid::new_v4();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let Some(Ok(Message::Text(authentication))) = socket.next().await else {
                panic!("missing authentication");
            };
            assert!(
                matches!(serde_json::from_str::<BackupToConsole>(&authentication).unwrap(),
                BackupToConsole::Authenticate { deployment_id: received, .. } if received == deployment_id)
            );
            socket
                .send(Message::Text(
                    serde_json::to_string(&ConsoleToBackup::Authenticated)
                        .unwrap()
                        .into(),
                ))
                .await
                .unwrap();
            let Some(Ok(Message::Text(report))) = socket.next().await else {
                panic!("missing status report");
            };
            assert!(
                matches!(serde_json::from_str::<BackupToConsole>(&report).unwrap(),
                BackupToConsole::Status { status } if status.deployment_id == deployment_id && status.last_success_at_unix == Some(1_000))
            );
            socket
                .send(Message::Text(
                    serde_json::to_string(&ConsoleToBackup::Trigger {
                        task_id: manual_task_id,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();
            let Some(Ok(Message::Text(result))) = socket.next().await else {
                panic!("missing trigger result");
            };
            assert!(
                matches!(serde_json::from_str::<BackupToConsole>(&result).unwrap(),
                BackupToConsole::TriggerResult { task_id, accepted: true, code: None } if task_id == manual_task_id)
            );
            socket.close(None).await.unwrap();
        });
        let (manual_sender, manual_receiver) = std::sync::mpsc::channel::<ManualRequest>();
        let manual_worker = std::thread::spawn(move || {
            let request = manual_receiver
                .recv_timeout(Duration::from_secs(5))
                .unwrap();
            assert_eq!(request.task_id, manual_task_id);
            request.result.send(Ok(())).unwrap();
        });
        let (socket, _) = tokio_tungstenite::connect_async(format!("ws://{listen_address}"))
            .await
            .unwrap();
        let control = BackupControlConfig {
            console_url: format!("ws://{listen_address}"),
            console_ca_file: status_path.clone(),
            token: "a".repeat(64),
        };
        let stopping = AtomicBool::new(false);
        let executing = AtomicBool::new(false);
        let result = tokio::time::timeout(
            Duration::from_secs(10),
            session(
                socket,
                &control,
                &status_path,
                deployment_id,
                &manual_sender,
                &stopping,
                &executing,
            ),
        )
        .await
        .unwrap();
        assert!(result.is_err()); // The test peer closes after confirming the result.
        server.await.unwrap();
        manual_worker.join().unwrap();
        assert!(fs::metadata(status_path).unwrap().is_file());
    }

    fn make_private(path: &std::path::Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::{os::windows::process::CommandExt, process::Command};
            let identity = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(identity.status.success());
            let grant = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(identity.stdout).unwrap().trim()
            );
            let result = Command::new("icacls")
                .arg(path)
                .args(["/inheritance:r", "/grant:r", &grant, "*S-1-5-18:(OI)(CI)F"])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(result.status.success());
        }
    }
}
