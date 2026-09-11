use crate::console_api_error::ConsoleApiError;
use crate::net_service::console_service_conn::{
    ConsoleServiceConn, ConsoleServiceConnPtr, ConsoleServiceConnVo,
};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct ConsoleServiceConnManager {
    state: Mutex<ConnectionState>,
    next_epoch: AtomicU64,
}

#[derive(Default)]
struct ConnectionState {
    connections: HashMap<String, ConsoleServiceConnPtr>,
    /// Epochs are device-scoped. A reconnect of D2 must never cancel the
    /// delayed disconnect reconciliation for D1.
    epochs: HashMap<String, u64>,
}

impl ConsoleServiceConnManager {
    pub fn new() -> Self {
        Self {
            state: Mutex::new(ConnectionState::default()),
            next_epoch: AtomicU64::new(0),
        }
    }

    /// Insert conn and return the new epoch. A stale connection for the same
    /// device_id is proactively closed so its recv loop ends.
    pub async fn add_conn(&self, device_id: String, conn: ConsoleServiceConnPtr) -> u64 {
        let epoch = self.next_epoch.fetch_add(1, Ordering::Relaxed) + 1;
        let old = {
            let mut state = self.state.lock().await;
            state.epochs.insert(device_id.clone(), epoch);
            state.connections.insert(device_id, conn)
        };
        if let Some(old) = old {
            old.lock().await.close().await;
        }
        epoch
    }

    pub async fn device_epoch(&self, device_id: &str) -> Option<u64> {
        self.state.lock().await.epochs.get(device_id).copied()
    }

    /// Compare-and-remove: only evict if the stored connection is the exact
    /// same Arc — otherwise a slow recv_task of an old connection would wipe
    /// the newer connection of a reconnecting device.
    pub async fn remove_conn(&self, device_id: String, conn: &ConsoleServiceConnPtr) {
        let mut state = self.state.lock().await;
        if state
            .connections
            .get(&device_id)
            .is_some_and(|cur| Arc::ptr_eq(cur, conn))
        {
            state.connections.remove(&device_id);
        }
    }

    pub async fn get_conn(
        &self,
        device_id: String,
    ) -> Result<ConsoleServiceConnPtr, ConsoleApiError> {
        let conn = self.state.lock().await.connections.get(&device_id).cloned();
        if let Some(conn) = conn {
            Ok(conn)
        } else {
            Err(ConsoleApiError::DeviceNotFound)
        }
    }

    pub async fn get_conn_info(
        &self,
        device_id: String,
    ) -> Result<ConsoleServiceConnVo, ConsoleApiError> {
        let conn = self.get_conn(device_id).await?;
        let conn = conn.lock().await.clone();
        Ok(conn.as_info())
    }

    pub async fn node_endpoint(&self, device_id: String) -> Option<(String, i32)> {
        let connection = self.get_conn(device_id.clone()).await.ok()?;
        let endpoint = {
            let guard = connection.lock().await;
            if px_base::get_current_timestamp().saturating_sub(guard.last_update_timestamp) > 30_000
            {
                return None;
            }
            guard
                .node_endpoints
                .as_ref()
                .and_then(|report| report.desktop_endpoint())
        };
        let current = self.get_conn(device_id).await.ok()?;
        if !Arc::ptr_eq(&connection, &current) {
            return None;
        }
        endpoint
    }

    pub async fn get_all_conn(&self) -> Result<Vec<ConsoleServiceConn>, ConsoleApiError> {
        let mut all_conn = Vec::new();
        for conn in self.state.lock().await.connections.values() {
            all_conn.push(conn.lock().await.clone());
        }
        // An empty collection is a normal state while no Service is online.
        // Callers need the list itself to decide how to present offline nodes.
        Ok(all_conn)
    }

    pub async fn get_all_conn_info(&self) -> Result<Vec<ConsoleServiceConnVo>, ConsoleApiError> {
        let mut all_conn = Vec::new();
        for conn in self.state.lock().await.connections.values() {
            all_conn.push(conn.lock().await.as_info());
        }
        // No online Service is not an API error: return [] so schedules and
        // known-but-offline devices remain manageable from the Console web UI.
        Ok(all_conn)
    }

    pub async fn get_all_conn_count(&self) -> usize {
        self.state.lock().await.connections.len()
    }

    pub async fn is_service_online(&self, device_id: String) -> Result<bool, ConsoleApiError> {
        for id in self.state.lock().await.connections.keys() {
            if *id == device_id {
                return Ok(true);
            }
        }
        Ok(false)
    }

    pub async fn broadcast_rtc_ice_config_changed(&self, revision: u64, changed_at: i64) -> usize {
        let connections: Vec<_> = self
            .state
            .lock()
            .await
            .connections
            .values()
            .cloned()
            .collect();
        let mut delivered = 0;
        for connection in connections {
            if connection
                .lock()
                .await
                .send_rtc_ice_config_changed(revision, changed_at)
                .await
            {
                delivered += 1;
            }
        }
        delivered
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::console_context::ConsoleContext;
    use prost::Message as ProstMessage;
    use std::sync::Arc;

    fn endpoint_report(host: &str) -> protocol::console_service::NodeEndpoints {
        protocol::console_service::NodeEndpoints {
            schema_version: 1,
            access_host: host.into(),
            desktop_port: 4601,
            application_port_start: 4613,
            application_port_end: 4998,
            rtc_port_start: 5000,
            rtc_port_end: 5031,
        }
    }

    #[tokio::test]
    async fn node_endpoint_is_scoped_to_live_generation_and_node() {
        let manager = ConsoleServiceConnManager::new();
        let first = make_conn("node-a", "key-a");
        let second = make_conn("node-b", "key-b");
        for (conn, host) in [(&first, "a.example.com"), (&second, "b.example.com")] {
            let mut guard = conn.lock().await;
            guard.node_endpoints = Some(endpoint_report(host));
            guard.last_update_timestamp = px_base::get_current_timestamp();
        }
        manager.add_conn("node-a".into(), first.clone()).await;
        manager.add_conn("node-b".into(), second.clone()).await;
        assert_eq!(
            manager.node_endpoint("node-a".into()).await,
            Some(("a.example.com".into(), 4601))
        );
        assert_eq!(
            manager.node_endpoint("node-b".into()).await,
            Some(("b.example.com".into(), 4601))
        );
        let replacement = make_conn("node-a", "key-a");
        manager.add_conn("node-a".into(), replacement.clone()).await;
        first.lock().await.node_endpoints = Some(endpoint_report("stale.example.com"));
        manager.remove_conn("node-a".into(), &first).await;
        assert_eq!(manager.node_endpoint("node-a".into()).await, None);
        {
            let mut guard = replacement.lock().await;
            guard.node_endpoints = Some(endpoint_report("new.example.com"));
            guard.last_update_timestamp = px_base::get_current_timestamp();
        }
        assert_eq!(
            manager.node_endpoint("node-a".into()).await,
            Some(("new.example.com".into(), 4601))
        );
        replacement.lock().await.last_update_timestamp = px_base::get_current_timestamp() - 31_000;
        assert_eq!(manager.node_endpoint("node-a".into()).await, None);
        manager.remove_conn("node-b".into(), &second).await;
        assert_eq!(manager.node_endpoint("node-b".into()).await, None);
    }

    #[tokio::test]
    async fn node_endpoint_rejects_impersonation_and_invalid_reports() {
        let connection = make_conn("node-a", "key-a");
        let mut guard = connection.lock().await;
        let mut message = protocol::console_service::ConsoleServiceMessage {
            msg_type: protocol::console_service::ConsoleServiceMessageType::KConsoleServiceHeartBeat
                as i32,
            device_id: "node-a".into(),
            heartbeat: Some(protocol::console_service::ConsoleServiceHeartBeat {
                device_id: "node-b".into(),
                node_endpoints: Some(endpoint_report("a.example.com")),
                ..Default::default()
            }),
            ..Default::default()
        };
        assert!(
            !guard
                .process_message("test".into(), message.encode_to_vec().into())
                .await
        );
        message.heartbeat.as_mut().unwrap().device_id = "node-a".into();
        assert!(
            guard
                .process_message("test".into(), message.encode_to_vec().into())
                .await
        );
        assert_eq!(guard.node_endpoints, Some(endpoint_report("a.example.com")));
        message
            .heartbeat
            .as_mut()
            .unwrap()
            .node_endpoints
            .as_mut()
            .unwrap()
            .desktop_port = 0;
        assert!(
            !guard
                .process_message("test".into(), message.encode_to_vec().into())
                .await
        );
        assert!(guard.node_endpoints.is_none());
    }

    fn make_conn(device_id: &str, appkey: &str) -> ConsoleServiceConnPtr {
        Arc::new(Mutex::new(ConsoleServiceConn {
            context: Arc::new(Mutex::new(ConsoleContext::new())),
            sender: None,
            device_id: device_id.to_string(),
            appkey: appkey.to_string(),
            version: "1.0.0".to_string(),
            rdp_available: false,
            rdp_domain: String::new(),
            rdp_proxy_certificate_sha256: String::new(),
            hello_timestamp: 100,
            last_update_timestamp: 200,
            hb_index: 3,
            render_alive: true,
            auth_info_json: "{}".to_string(),
            instances_json: "[]".to_string(),
            logical_sessions_json: "[]".to_string(),
            node_endpoints: None,
        }))
    }

    #[tokio::test]
    async fn add_remove_and_count_conn() {
        let mgr = ConsoleServiceConnManager::new();
        assert_eq!(mgr.get_all_conn_count().await, 0);

        let c1 = make_conn("d1", "appkey-1");
        mgr.add_conn("d1".to_string(), c1.clone()).await;
        mgr.add_conn("d2".to_string(), make_conn("d2", "appkey-1"))
            .await;
        assert_eq!(mgr.get_all_conn_count().await, 2);
        assert_eq!(mgr.is_service_online("d1".to_string()).await.unwrap(), true);

        mgr.remove_conn("d1".to_string(), &c1).await;
        assert_eq!(mgr.get_all_conn_count().await, 1);
        assert!(mgr.get_conn("d1".to_string()).await.is_err());
        assert!(mgr.get_conn("d2".to_string()).await.is_ok());
    }

    #[tokio::test]
    async fn remove_conn_keeps_newer_connection() {
        let mgr = ConsoleServiceConnManager::new();
        let old = make_conn("d1", "appkey-1");
        let e1 = mgr.add_conn("d1".to_string(), old.clone()).await;
        // Same device reconnects: new conn replaces the old one, epoch bumps.
        let new = make_conn("d1", "appkey-1");
        let e2 = mgr.add_conn("d1".to_string(), new.clone()).await;
        assert!(e2 > e1);
        assert_eq!(mgr.get_all_conn_count().await, 1);

        // Stale recv_task of the old connection must not evict the new one.
        mgr.remove_conn("d1".to_string(), &old).await;
        assert_eq!(mgr.get_all_conn_count().await, 1);
        assert!(Arc::ptr_eq(
            &mgr.get_conn("d1".to_string()).await.unwrap(),
            &new
        ));

        // Removing with the current conn works.
        mgr.remove_conn("d1".to_string(), &new).await;
        assert_eq!(mgr.get_all_conn_count().await, 0);
    }

    #[tokio::test]
    async fn epochs_are_scoped_to_each_device() {
        let mgr = ConsoleServiceConnManager::new();
        let d1_epoch = mgr
            .add_conn("d1".to_string(), make_conn("d1", "appkey-1"))
            .await;
        let _d2_epoch = mgr
            .add_conn("d2".to_string(), make_conn("d2", "appkey-1"))
            .await;

        assert_eq!(mgr.device_epoch("d1").await, Some(d1_epoch));
        let d1_reconnected = mgr
            .add_conn("d1".to_string(), make_conn("d1", "appkey-1"))
            .await;
        assert!(d1_reconnected > d1_epoch);
        assert_eq!(mgr.device_epoch("d1").await, Some(d1_reconnected));
    }

    #[tokio::test]
    async fn get_all_conn_info_returns_empty_or_vos() {
        let mgr = ConsoleServiceConnManager::new();
        assert!(mgr.get_all_conn_info().await.unwrap().is_empty());
        assert!(mgr.get_all_conn().await.unwrap().is_empty());

        mgr.add_conn("d1".to_string(), make_conn("d1", "appkey-1"))
            .await;
        let infos = mgr.get_all_conn_info().await.unwrap();
        assert_eq!(infos.len(), 1);
        let info = &infos[0];
        assert_eq!(info.device_id, "d1");
        assert_eq!(info.version, "1.0.0");
        assert_eq!(info.hello_timestamp, 100);
        assert_eq!(info.last_update_timestamp, 200);
        assert_eq!(info.hb_index, 3);
        assert!(info.render_alive);
        assert_eq!(info.auth_info_json, "{}");

        let info = mgr.get_conn_info("d1".to_string()).await.unwrap();
        assert_eq!(info.device_id, "d1");
    }

    #[tokio::test]
    async fn process_message_updates_state() {
        let conn = make_conn("d1", "appkey-1");
        {
            let mut c = conn.lock().await;

            let mut hello = protocol::console_service::ConsoleServiceMessage::default();
            hello.set_msg_type(
                protocol::console_service::ConsoleServiceMessageType::KConsoleServiceHello,
            );
            hello.hello = Some(protocol::console_service::ConsoleServiceHello {
                device_id: "d1".to_string(),
                appkey: "appkey-1".to_string(),
                version: "2.0.0".to_string(),
                rdp_available: false,
                rdp_domain: String::new(),
                rdp_proxy_certificate_sha256: String::new(),
            });
            assert!(
                c.process_message(
                    "test".to_string(),
                    axum::body::Bytes::from(hello.encode_to_vec())
                )
                .await
            );
            assert_eq!(c.version, "2.0.0");
            assert!(c.hello_timestamp > 0);
            assert_eq!(c.last_update_timestamp, c.hello_timestamp);

            let mut hb = protocol::console_service::ConsoleServiceMessage::default();
            hb.set_msg_type(
                protocol::console_service::ConsoleServiceMessageType::KConsoleServiceHeartBeat,
            );
            hb.heartbeat = Some(protocol::console_service::ConsoleServiceHeartBeat {
                node_endpoints: None,
                hb_index: 42,
                device_id: "d1".to_string(),
                render_alive: false,
                auth_info_json: "{\"a\":1}".to_string(),
                instances_json: "[]".to_string(),
                logical_sessions_json: "[]".to_string(),
            });
            assert!(
                c.process_message(
                    "test".to_string(),
                    axum::body::Bytes::from(hb.encode_to_vec())
                )
                .await
            );
            assert_eq!(c.hb_index, 42);
            assert!(!c.render_alive);
            assert_eq!(c.auth_info_json, "{\"a\":1}");

            // garbage payload -> parse error -> false
            assert!(
                !c.process_message(
                    "test".to_string(),
                    axum::body::Bytes::from(vec![0xff, 0xff])
                )
                .await
            );
        }
    }
}
