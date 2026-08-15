use crate::net_service::spvr_service_conn::{
    SpvrServiceConn, SpvrServiceConnPtr, SpvrServiceConnVo,
};
use crate::spvr_api_error::SpvrApiError;
use egui::ahash::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct SpvrServiceConnManager {
    connections: Mutex<HashMap<String, SpvrServiceConnPtr>>,
    /// Bumped on every add_conn; disconnect handlers capture it to detect a
    /// reconnect during the delayed-reconcile window.
    epoch: AtomicU64,
}

impl SpvrServiceConnManager {
    pub fn new() -> Self {
        Self {
            connections: Mutex::new(Default::default()),
            epoch: AtomicU64::new(0),
        }
    }

    /// Insert conn and return the new epoch. A stale connection for the same
    /// device_id is proactively closed so its recv loop ends.
    pub async fn add_conn(&self, device_id: String, conn: SpvrServiceConnPtr) -> u64 {
        let old = self.connections.lock().await.insert(device_id, conn);
        let epoch = self.epoch.fetch_add(1, Ordering::Relaxed) + 1;
        if let Some(old) = old {
            old.lock().await.close().await;
        }
        epoch
    }

    pub fn epoch(&self) -> u64 {
        self.epoch.load(Ordering::Relaxed)
    }

    /// Compare-and-remove: only evict if the stored connection is the exact
    /// same Arc — otherwise a slow recv_task of an old connection would wipe
    /// the newer connection of a reconnecting device.
    pub async fn remove_conn(&self, device_id: String, conn: &SpvrServiceConnPtr) {
        let mut conns = self.connections.lock().await;
        if conns
            .get(&device_id)
            .is_some_and(|cur| Arc::ptr_eq(cur, conn))
        {
            conns.remove(&device_id);
        }
    }

    pub async fn get_conn(&self, device_id: String) -> Result<SpvrServiceConnPtr, SpvrApiError> {
        let conn = self.connections.lock().await.get(&device_id).cloned();
        if let Some(conn) = conn {
            Ok(conn)
        } else {
            Err(SpvrApiError::DeviceNotFound)
        }
    }

    pub async fn get_conn_info(
        &self,
        device_id: String,
    ) -> Result<SpvrServiceConnVo, SpvrApiError> {
        let conn = self.get_conn(device_id).await?;
        let conn = conn.lock().await.clone();
        Ok(conn.as_info())
    }

    pub async fn get_all_conn(&self) -> Result<Vec<SpvrServiceConn>, SpvrApiError> {
        let mut all_conn = Vec::new();
        for conn in self.connections.lock().await.values() {
            all_conn.push(conn.lock().await.clone());
        }
        if all_conn.is_empty() {
            Err(SpvrApiError::ConnectionNotFound)
        } else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_info(&self) -> Result<Vec<SpvrServiceConnVo>, SpvrApiError> {
        let mut all_conn = Vec::new();
        for conn in self.connections.lock().await.values() {
            all_conn.push(conn.lock().await.as_info());
        }
        if all_conn.is_empty() {
            Err(SpvrApiError::ConnectionNotFound)
        } else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_count(&self) -> usize {
        self.connections.lock().await.len()
    }

    pub async fn is_service_online(&self, device_id: String) -> Result<bool, SpvrApiError> {
        for id in self.connections.lock().await.keys() {
            if *id == device_id {
                return Ok(true);
            }
        }
        Ok(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::spvr_context::SpvrContext;
    use prost::Message as ProstMessage;
    use std::sync::Arc;

    fn make_conn(device_id: &str, appkey: &str) -> SpvrServiceConnPtr {
        Arc::new(Mutex::new(SpvrServiceConn {
            context: Arc::new(Mutex::new(SpvrContext::new())),
            sender: None,
            device_id: device_id.to_string(),
            appkey: appkey.to_string(),
            version: "1.0.0".to_string(),
            hello_timestamp: 100,
            last_update_timestamp: 200,
            hb_index: 3,
            render_alive: true,
            auth_info_json: "{}".to_string(),
            instances_json: "[]".to_string(),
        }))
    }

    #[tokio::test]
    async fn add_remove_and_count_conn() {
        let mgr = SpvrServiceConnManager::new();
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
        let mgr = SpvrServiceConnManager::new();
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
    async fn get_all_conn_info_returns_vos() {
        let mgr = SpvrServiceConnManager::new();
        assert!(mgr.get_all_conn_info().await.is_err());

        mgr.add_conn("d1".to_string(), make_conn("d1", "appkey-1"))
            .await;
        let infos = mgr.get_all_conn_info().await.unwrap();
        assert_eq!(infos.len(), 1);
        let info = &infos[0];
        assert_eq!(info.device_id, "d1");
        assert_eq!(info.appkey, "appkey-1");
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

            let mut hello = protocol::spvr_service::SpvrServiceMessage::default();
            hello.set_msg_type(protocol::spvr_service::SpvrServiceMessageType::KSpvrServiceHello);
            hello.hello = Some(protocol::spvr_service::SpvrServiceHello {
                device_id: "d1".to_string(),
                appkey: "appkey-1".to_string(),
                version: "2.0.0".to_string(),
            });
            assert!(
                c.process_message("test".to_string(), axum::body::Bytes::from(hello.encode_to_vec()))
                    .await
            );
            assert_eq!(c.version, "2.0.0");
            assert!(c.hello_timestamp > 0);
            assert_eq!(c.last_update_timestamp, c.hello_timestamp);

            let mut hb = protocol::spvr_service::SpvrServiceMessage::default();
            hb.set_msg_type(
                protocol::spvr_service::SpvrServiceMessageType::KSpvrServiceHeartBeat,
            );
            hb.heartbeat = Some(protocol::spvr_service::SpvrServiceHeartBeat {
                hb_index: 42,
                device_id: "d1".to_string(),
                render_alive: false,
                auth_info_json: "{\"a\":1}".to_string(),
                instances_json: "[]".to_string(),
            });
            assert!(
                c.process_message("test".to_string(), axum::body::Bytes::from(hb.encode_to_vec()))
                    .await
            );
            assert_eq!(c.hb_index, 42);
            assert!(!c.render_alive);
            assert_eq!(c.auth_info_json, "{\"a\":1}");

            // garbage payload -> parse error -> false
            assert!(
                !c.process_message("test".to_string(), axum::body::Bytes::from(vec![0xff, 0xff]))
                    .await
            );
        }
    }
}
