use crate::server::RelayServerState;
use futures_util::{SinkExt, StreamExt};
use px_relay_control_protocol::{RelayReport, RelayRequest, RelayResponse, MAX_MESSAGE_BYTES};
use std::{sync::Arc, time::Duration};
use tokio_tungstenite::{
    connect_async_tls_with_config, tungstenite::Message, Connector, MaybeTlsStream, WebSocketStream,
};
use tokio_util::sync::CancellationToken;

const CONNECT_RETRY: Duration = Duration::from_secs(1);
const RESPONSE_DEADLINE: Duration = Duration::from_secs(15);
const REPORT_INTERVAL: Duration = Duration::from_secs(5);

type ControlSocket = WebSocketStream<MaybeTlsStream<tokio::net::TcpStream>>;

enum ManagedRelay {
    Rooms(RelayServerState),
    Iroh(crate::iroh::IrohRelayManagement),
}

impl ManagedRelay {
    fn set_draining(&self, draining: bool) {
        match self {
            Self::Rooms(state) => state.set_draining(draining),
            Self::Iroh(state) => state.set_draining(draining),
        }
    }

    async fn report(&self, sequence: u64, product_version_code: u32) -> Result<RelayReport, ()> {
        match self {
            Self::Iroh(state) => Ok(state.report(sequence, product_version_code)),
            Self::Rooms(state) => {
                let snapshot = state.snapshot().await;
                Ok(RelayReport {
                    sequence,
                    product_version_code,
                    draining: state.is_draining(),
                    max_connections: u32::try_from(state.config().max_connections)
                        .map_err(|_| ())?,
                    current_connections: u32::try_from(snapshot.connections).map_err(|_| ())?,
                    max_rooms: Some(u32::try_from(state.config().max_rooms).map_err(|_| ())?),
                    current_rooms: Some(u32::try_from(snapshot.rooms).map_err(|_| ())?),
                    iroh_qad_port: None,
                    uploaded_bytes: snapshot.uploaded_payload_bytes,
                    forwarded_bytes: snapshot.forwarded_payload_bytes,
                })
            }
        }
    }
}

pub async fn run(state: RelayServerState, cancellation: CancellationToken) {
    let Some(control_plane) = state.config().control_plane.clone() else {
        return;
    };
    run_managed(ManagedRelay::Rooms(state), control_plane, cancellation).await;
}

pub async fn run_iroh(
    state: crate::iroh::IrohRelayManagement,
    control_plane: crate::config::ControlPlaneConfig,
    cancellation: CancellationToken,
) {
    run_managed(ManagedRelay::Iroh(state), control_plane, cancellation).await;
}

async fn run_managed(
    state: ManagedRelay,
    control_plane: crate::config::ControlPlaneConfig,
    cancellation: CancellationToken,
) {
    state.set_draining(true);
    loop {
        if cancellation.is_cancelled() {
            return;
        }
        let connector = control_plane
            .tls_config
            .as_ref()
            .map(|tls_config| Connector::Rustls(Arc::clone(tls_config)));
        let connected = tokio::select! {
            _ = cancellation.cancelled() => return,
            connected = tokio::time::timeout(RESPONSE_DEADLINE, connect_async_tls_with_config(&control_plane.url, None, false, connector)) => connected,
        };
        match connected {
            Ok(Ok((socket, _))) => {
                let completed = tokio::select! {
                    _ = cancellation.cancelled() => return,
                    completed = run_session(&state, &control_plane, socket, &cancellation) => completed,
                };
                if completed.is_err() {
                    tracing::warn!(
                        "Relay control connection closed; new admissions remain drained"
                    );
                }
            }
            Ok(Err(error)) => tracing::warn!(%error, "Relay control connection failed"),
            Err(_) => tracing::warn!("Relay control connection timed out"),
        }
        state.set_draining(true);
        tokio::select! {
            _ = cancellation.cancelled() => return,
            _ = tokio::time::sleep(CONNECT_RETRY) => {}
        }
    }
}

async fn run_session(
    state: &ManagedRelay,
    control_plane: &crate::config::ControlPlaneConfig,
    mut socket: ControlSocket,
    cancellation: &CancellationToken,
) -> Result<(), ()> {
    send(
        &mut socket,
        &RelayRequest::Authenticate {
            request_id: 1,
            relay_token: control_plane.token.as_str().to_string(),
        },
    )
    .await?;
    let RelayResponse::Authenticated {
        request_id: 1,
        desired_draining,
        ..
    } = receive(&mut socket, cancellation).await?
    else {
        return Err(());
    };
    state.set_draining(desired_draining);
    let mut request_id = 1_u64;
    let mut sequence = 0_u64;
    let mut report_interval = tokio::time::interval(REPORT_INTERVAL);
    report_interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    let mut report_immediately = false;
    loop {
        if !report_immediately {
            tokio::select! {
                biased;
                _ = cancellation.cancelled() => {
                    let _ = socket.close(None).await;
                    return Ok(());
                }
                _ = report_interval.tick() => {}
            }
        }
        request_id = request_id.checked_add(1).ok_or(())?;
        sequence = sequence.checked_add(1).ok_or(())?;
        let report = state
            .report(sequence, control_plane.product_version_code)
            .await?;
        let reported_draining = report.draining;
        send(&mut socket, &RelayRequest::Report { request_id, report }).await?;
        match receive(&mut socket, cancellation).await? {
            RelayResponse::Reported {
                request_id: response_id,
                desired_draining,
            } if response_id == request_id => {
                report_immediately = reported_draining != desired_draining;
                state.set_draining(desired_draining);
            }
            _ => return Err(()),
        }
    }
}

async fn send(socket: &mut ControlSocket, request: &RelayRequest) -> Result<(), ()> {
    let encoded = serde_json::to_string(request).map_err(|_| ())?;
    if encoded.len() > MAX_MESSAGE_BYTES {
        return Err(());
    }
    tokio::time::timeout(
        RESPONSE_DEADLINE,
        socket.send(Message::Text(encoded.into())),
    )
    .await
    .map_err(|_| ())?
    .map_err(|_| ())
}

async fn receive(
    socket: &mut ControlSocket,
    cancellation: &CancellationToken,
) -> Result<RelayResponse, ()> {
    loop {
        let message = tokio::select! {
            biased;
            _ = cancellation.cancelled() => return Err(()),
            received = tokio::time::timeout(RESPONSE_DEADLINE, socket.next()) => {
                received.map_err(|_| ())?.ok_or(())?.map_err(|_| ())?
            }
        };
        match message {
            Message::Text(text) if text.len() <= MAX_MESSAGE_BYTES => {
                return serde_json::from_str(&text).map_err(|_| ());
            }
            Message::Ping(payload) => socket.send(Message::Pong(payload)).await.map_err(|_| ())?,
            Message::Pong(_) => {}
            Message::Close(_) | Message::Binary(_) | Message::Frame(_) | Message::Text(_) => {
                return Err(())
            }
        }
    }
}
