use crate::device::spvr_device_keys::KEY_DEVICE_ID;
use crate::event::spvr_event::SpvrEvent;
use crate::gSpvrDatabase;
use crate::net_client::spvr_client_conn::{SpvrClientConn, SpvrClientConnPtr, SpvrClientConnVo};
use crate::spvr_api_error::SpvrApiError;
use egui::ahash::HashMap;
use mongodb::bson::doc;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::Arc;
use tokio::sync::Mutex;
use tokio_stream::StreamExt;

/// A reservation for one client stream slot.
///
/// The reservation is created by the token filter before the WebSocket upgrade.
/// It is either claimed by the connection handler after the socket is registered
/// (by calling `forget`) or released automatically when the guard is dropped.
/// Cloning a reservation does **not** consume an additional slot; it only
/// creates another view of the same reserved slot.
pub struct StreamReservation {
    mgr: Arc<SpvrClientConnManager>,
    claimed: Arc<AtomicBool>,
}

impl StreamReservation {
    /// Consume the reservation without releasing the reserved slot. Must be
    /// called once the connection has been successfully registered in
    /// `connections`.
    pub fn forget(self) {
        self.claimed.store(true, Ordering::SeqCst);
        std::mem::forget(self);
    }
}

impl Clone for StreamReservation {
    fn clone(&self) -> Self {
        Self {
            mgr: self.mgr.clone(),
            claimed: self.claimed.clone(),
        }
    }
}

impl Drop for StreamReservation {
    fn drop(&mut self) {
        if !self.claimed.load(Ordering::SeqCst) {
            self.mgr.release_stream();
        }
    }
}

pub struct SpvrClientConnManager {
    connections: Mutex<HashMap<String, SpvrClientConnPtr>>,
    /// Number of stream slots currently reserved (including sockets that have
    /// not finished upgrading yet). This is kept in sync with the map size so
    /// that the pre-upgrade limit check is atomic and not overly permissive.
    reserved_streams: AtomicI32,
}

impl SpvrClientConnManager {
    pub fn new() -> SpvrClientConnManager {
        Self {
            connections: Mutex::new(Default::default()),
            reserved_streams: AtomicI32::new(0),
        }
    }

    /// Try to atomically reserve one stream slot. Returns `None` if `max_streams`
    /// has already been reached.
    pub fn try_reserve_stream(self: &Arc<Self>, max_streams: i32) -> Option<StreamReservation> {
        loop {
            let current = self.reserved_streams.load(Ordering::SeqCst);
            if current >= max_streams {
                return None;
            }
            match self.reserved_streams.compare_exchange(
                current,
                current + 1,
                Ordering::SeqCst,
                Ordering::SeqCst,
            ) {
                Ok(_) => {
                    return Some(StreamReservation {
                        mgr: self.clone(),
                        claimed: Arc::new(AtomicBool::new(false)),
                    });
                }
                Err(_) => continue,
            }
        }
    }

    pub fn release_stream(&self) {
        self.reserved_streams.fetch_sub(1, Ordering::SeqCst);
    }

    pub async fn add_conn(&self, conn_id: String, conn: SpvrClientConnPtr) {
        self.connections.lock().await.insert(conn_id, conn);
    }

    pub async fn remove_conn(&self, conn_id: String) {
        self.connections.lock().await.remove(&conn_id);
        self.release_stream();
    }

    pub async fn get_alive_connections_ptr(&self) -> Vec<SpvrClientConnPtr> {
        tracing::info!(
            "alive connections count: {}",
            self.connections.lock().await.len()
        );
        let conns: Vec<SpvrClientConnPtr> =
            self.connections.lock().await.values().cloned().collect();
        tracing::info!("- alive connections count: {}", conns.len());
        let mut new_conns = Vec::new();
        for conn in &conns {
            if conn.lock().await.connection_alive {
                new_conns.push(conn.clone());
            }
        }
        new_conns
    }

    pub async fn get_alive_connections(&self) -> Vec<SpvrClientConnVo> {
        tracing::info!(
            "alive connections count: {}",
            self.connections.lock().await.len()
        );
        let conns: Vec<SpvrClientConnPtr> =
            self.connections.lock().await.values().cloned().collect();
        tracing::info!("- alive connections count: {}", conns.len());
        let mut new_conns = Vec::new();
        for conn in &conns {
            if conn.lock().await.connection_alive {
                // let conn = {
                //     let guard = conn.lock().await;
                //     guard.clone()
                // };
                new_conns.push(conn.lock().await.as_vo());
            }
        }
        new_conns
    }

    pub async fn count_alive_connections(&self) -> u32 {
        self.reserved_streams.load(Ordering::SeqCst) as u32
    }

    // -- DB --
    pub async fn insert_conn(&self, conn: SpvrClientConnVo) {
        let c_client_conn = gSpvrDatabase.lock().await.client_conn();
        let _ = c_client_conn.lock().await.insert_one(conn).await;
    }

    pub async fn update_conn(&self, conn_id: String, timestamp: i64) {
        let c_client_conn = gSpvrDatabase.lock().await.client_conn();

        let filter = doc! {
            "conn_id": conn_id,
        };
        let update = doc! {
            "$set" : doc! {
                "last_update_timestamp": timestamp,
                "readable_update_ts": gr_base::format_readable_timestamp(timestamp),
            }
        };
        let _ = c_client_conn.lock().await.update_one(filter, update).await;
    }

    pub async fn query_client_conns(
        &self,
        device_id: String,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<SpvrClientConnVo>, SpvrApiError> {
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;

        let r = gSpvrDatabase
            .lock()
            .await
            .client_conn()
            .lock()
            .await
            .find(doc! {
                KEY_DEVICE_ID: device_id,
            })
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(err) = r {
            tracing::error!("failed to query clients: {}", err);
            return Err(SpvrApiError::DatabaseError);
        }
        let mut cursor = r.unwrap();
        let mut clients_conn: Vec<SpvrClientConnVo> = Vec::new();
        while let Some(conn) = cursor.next().await {
            if let Err(err) = conn {
                tracing::error!("failed to query client connection: {}", err);
                break;
            }
            clients_conn.push(conn.unwrap());
        }
        Ok(clients_conn)
    }

    pub async fn query_conns(
        &self,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<SpvrClientConnVo>, SpvrApiError> {
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;

        let r = gSpvrDatabase
            .lock()
            .await
            .client_conn()
            .lock()
            .await
            .find(doc! {})
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(err) = r {
            tracing::error!("failed to query clients: {}", err);
            return Err(SpvrApiError::DatabaseError);
        }
        let mut cursor = r.unwrap();
        let mut clients_conn: Vec<SpvrClientConnVo> = Vec::new();
        while let Some(conn) = cursor.next().await {
            if let Err(err) = conn {
                tracing::error!("failed to query connection: {}", err);
                break;
            }
            clients_conn.push(conn.unwrap());
        }
        Ok(clients_conn)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    #[tokio::test]
    async fn reservation_respects_max_streams() {
        let mgr = Arc::new(SpvrClientConnManager::new());

        let r1 = mgr.try_reserve_stream(2);
        assert!(r1.is_some());
        let r2 = mgr.try_reserve_stream(2);
        assert!(r2.is_some());
        let r3 = mgr.try_reserve_stream(2);
        assert!(r3.is_none());

        assert_eq!(mgr.count_alive_connections().await, 2);

        drop(r1);
        assert_eq!(mgr.count_alive_connections().await, 1);

        r2.unwrap().forget();
        assert_eq!(mgr.count_alive_connections().await, 1);
    }

    #[tokio::test]
    async fn add_and_remove_conn_updates_reserved_count() {
        let mgr = Arc::new(SpvrClientConnManager::new());
        let r = mgr.try_reserve_stream(1).expect("should reserve");

        // Simulate the handler registering the connection and claiming the slot.
        r.forget();
        assert_eq!(mgr.count_alive_connections().await, 1);

        mgr.remove_conn("conn-1".to_string()).await;
        assert_eq!(mgr.count_alive_connections().await, 0);
    }
}
