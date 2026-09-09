use crate::service_host::{ServiceRuntime, TicketRedeemRequest, TicketRedeemResult};
use futures_util::{stream, StreamExt};
use service_core::app_instance::AppInstanceRecord;
use service_core::AppInstanceState;
use std::{sync::Arc, time::Duration};
use tokio::sync::{mpsc, oneshot, Mutex};

#[derive(Clone)]
struct Candidate {
    record: AppInstanceRecord,
    logical_id: String,
    device_id: String,
}

// A bad snapshot is not equivalent to an idle workspace. The empty string is
// an explicit invalid binding, sent to the fail-closed path, never to Console.
fn logical_owner(snapshot: &str) -> Result<Option<String>, ()> {
    if snapshot.len() > 64 * 1024 {
        return Err(());
    }
    let rows: Vec<serde_json::Value> = serde_json::from_str(snapshot).map_err(|_| ())?;
    if rows.is_empty() {
        return Ok(None);
    }
    if rows.len() != 1 {
        return Err(());
    }
    let logical = rows[0]
        .get("logical_session_id")
        .and_then(|value| value.as_str())
        .ok_or(())?;
    if logical.is_empty() || logical.len() > 128 {
        return Err(());
    }
    Ok(Some(logical.to_string()))
}

fn matches_grant(candidate: &Candidate, result: &TicketRedeemResult) -> bool {
    result.ok
        && result.kind == "rdp_runtime"
        && result.instance_id == candidate.record.instance_id
        && result.logical_session_id == candidate.logical_id
        && result.device_id == candidate.device_id
}

async fn authorized(
    candidate: &Candidate,
    channel: Option<mpsc::Sender<TicketRedeemRequest>>,
) -> bool {
    let Some(channel) = channel else {
        return false;
    };
    if candidate.logical_id.is_empty() {
        return false;
    }
    let (response, receiver) = oneshot::channel();
    let request = TicketRedeemRequest {
        request_id: format!("rdp-auth-{:016x}", rand::random::<u64>()),
        ticket: String::new(),
        client_nonce: String::new(),
        instance_id: candidate.record.instance_id.clone(),
        rdp_logical_session_id: candidate.logical_id.clone(),
        response,
    };
    // Queue + Console response share one deadline. Cancellation drops the
    // receiver; the Console loop prunes its abandoned correlation entry.
    matches!(tokio::time::timeout(Duration::from_secs(3), async {
        channel.send(request).await.map_err(|_| ())?;
        receiver.await.map_err(|_| ())
    }).await, Ok(Ok(result)) if matches_grant(candidate, &result))
}

async fn check(runtime: Arc<Mutex<ServiceRuntime>>) {
    let (candidates, channel) = {
        let guard = runtime.lock().await;
        let device_id = guard
            .state
            .last_auth_info
            .as_ref()
            .map(|info| info.device_id.clone())
            .unwrap_or_default();
        let candidates = guard
            .app_registry
            .list()
            .into_iter()
            .filter(|record| {
                record.app_mode == "rdp"
                    && matches!(
                        record.state,
                        AppInstanceState::Running | AppInstanceState::Stopping
                    )
            })
            .filter_map(|record| {
                let snapshot = guard
                    .render_logical_sessions
                    .get(&format!("render_{}", record.listen_port))?;
                let logical_id = match logical_owner(snapshot) {
                    Ok(None) => return None,
                    Ok(Some(id)) => id,
                    Err(()) => String::new(),
                };
                Some(Candidate {
                    record: record.clone(),
                    logical_id,
                    device_id: device_id.clone(),
                })
            })
            .collect::<Vec<_>>();
        (
            candidates,
            guard
                .rdp_console_trusted
                .then(|| guard.ticket_redeem_tx.clone())
                .flatten(),
        )
    };
    let checks = stream::iter(candidates)
        .map(|candidate| {
            let channel = channel.clone();
            async move {
                let allowed = authorized(&candidate, channel).await;
                (candidate, allowed)
            }
        })
        .buffer_unordered(8);
    tokio::pin!(checks);
    while let Some((candidate, allowed)) = checks.next().await {
        if allowed {
            continue;
        }
        let current = {
            let guard = runtime.lock().await;
            guard.app_registry.get(&candidate.record.instance_id) == Some(&candidate.record)
                && guard
                    .render_logical_sessions
                    .get(&format!("render_{}", candidate.record.listen_port))
                    .is_some_and(|snapshot| {
                        logical_owner(snapshot).ok().flatten().unwrap_or_default()
                            == candidate.logical_id
                    })
        };
        if current {
            tracing::warn!(instance_id = %candidate.record.instance_id,
                "RDP live authorization rejected or unavailable; stopping runtime, preserving Windows session");
            if let Err(error) =
                ServiceRuntime::stop_app_instance(&runtime, &candidate.record.instance_id).await
            {
                tracing::warn!(%error, "RDP authorization shutdown failed; will retry while instance remains active");
            }
        }
    }
}

pub async fn run(runtime: Arc<Mutex<ServiceRuntime>>) -> Result<(), String> {
    let mut stop = runtime.lock().await.subscribe_stop();
    let mut interval = tokio::time::interval(Duration::from_secs(3));
    interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    loop {
        tokio::select! {
            _ = stop.recv() => return Ok(()),
            _ = interval.tick() => {
                tokio::select! {
                    _ = stop.recv() => return Ok(()),
                    _ = check(runtime.clone()) => {}
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn candidate() -> Candidate {
        Candidate {
            record: AppInstanceRecord {
                exit_detail: None,
                request_id: String::new(),
                instance_id: "inst-one".to_string(),
                app_id: String::new(),
                install_root: String::new(),
                game_exe_rel: String::new(),
                app_mode: "rdp".to_string(),
                rdp_workspace_id: "workspace-one".to_string(),
                rdp_node_id: String::new(),
                listen_port: 32014,
                pid: Some(1234),
                state: AppInstanceState::Running,
                error: String::new(),
                launch: service_core::RenderLaunchSpec {
                    work_dir: String::new(),
                    app_path: String::new(),
                    args: Vec::new(),
                },
                view_game_path: None,
                finished_at: None,
            },
            logical_id: "logical-one".to_string(),
            device_id: "node-one".to_string(),
        }
    }

    fn grant() -> TicketRedeemResult {
        TicketRedeemResult {
            ok: true,
            kind: "rdp_runtime".to_string(),
            instance_id: "inst-one".to_string(),
            logical_session_id: "logical-one".to_string(),
            device_id: "node-one".to_string(),
            ..Default::default()
        }
    }

    #[test]
    fn only_exact_runtime_grant_extends_authorization() {
        let candidate = candidate();
        assert!(matches_grant(&candidate, &grant()));
        for invalid in [
            TicketRedeemResult {
                ok: false,
                ..grant()
            },
            TicketRedeemResult {
                kind: "app_instance".to_string(),
                ..grant()
            },
            TicketRedeemResult {
                instance_id: "other".to_string(),
                ..grant()
            },
            TicketRedeemResult {
                logical_session_id: "other".to_string(),
                ..grant()
            },
            TicketRedeemResult {
                device_id: "other".to_string(),
                ..grant()
            },
        ] {
            assert!(!matches_grant(&candidate, &invalid));
        }
    }

    #[tokio::test]
    async fn runtime_request_contains_no_ticket_and_waits_for_exact_ack() {
        let candidate = candidate();
        let (sender, mut receiver) = mpsc::channel::<TicketRedeemRequest>(1);
        let check = tokio::spawn(async move { authorized(&candidate, Some(sender)).await });
        let request = receiver.recv().await.unwrap();
        assert!(request.ticket.is_empty() && request.client_nonce.is_empty());
        assert_eq!(request.rdp_logical_session_id, "logical-one");
        assert!(!check.is_finished());
        request.response.send(grant()).unwrap();
        assert!(check.await.unwrap());
    }

    #[tokio::test]
    async fn missing_console_and_cancelled_check_cannot_leave_a_live_waiter() {
        assert!(!authorized(&candidate(), None).await);
        let (sender, mut receiver) = mpsc::channel::<TicketRedeemRequest>(1);
        let check = tokio::spawn(async move { authorized(&candidate(), Some(sender)).await });
        let request = receiver.recv().await.unwrap();
        check.abort();
        assert!(check.await.unwrap_err().is_cancelled());
        assert!(request.response.is_closed());
    }
    #[test]
    fn idle_single_owner_and_invalid_snapshot_are_distinct() {
        assert_eq!(logical_owner("[]"), Ok(None));
        assert_eq!(
            logical_owner(r#"[{"logical_session_id":"one","transports":[]}]"#),
            Ok(Some("one".to_string()))
        );
        for invalid in [
            "{}",
            "[{}]",
            r#"[{"logical_session_id":""}]"#,
            r#"[{"logical_session_id":"one"},{"logical_session_id":"two"}]"#,
        ] {
            assert!(logical_owner(invalid).is_err());
        }
        assert!(logical_owner(&" ".repeat(65537)).is_err());
    }
}
