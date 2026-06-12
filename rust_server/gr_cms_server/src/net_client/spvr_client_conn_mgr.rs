use crate::event::spvr_event::SpvrEvent;
use crate::gSpvrDatabase;
use crate::net_client::spvr_client_conn::{SpvrClientConn, SpvrClientConnVo, SpvrClientConnPtr};
use crate::spvr_api_error::SpvrApiError;
use egui::ahash::HashMap;
use mongodb::bson::doc;
use tokio::sync::Mutex;
use tokio_stream::StreamExt;
use crate::device::spvr_device_keys::KEY_DEVICE_ID;

pub struct SpvrClientConnManager {
    connections: Mutex<HashMap<String, SpvrClientConnPtr>>,
}

impl SpvrClientConnManager {
    pub fn new() -> SpvrClientConnManager {
        Self {
            connections: Mutex::new(Default::default()),
        }
    }

    pub async fn add_conn(&self, conn_id: String, conn: SpvrClientConnPtr) {
        self.connections
            .lock().await
            .insert(conn_id, conn);
    }

    pub async fn remove_conn(&self, conn_id: String) {
        self.connections
            .lock().await
            .remove(&conn_id);
    }

    pub async fn get_alive_connections_ptr(&self) -> Vec<SpvrClientConnPtr> {
        tracing::info!("alive connections count: {}", self.connections.lock().await.len());
        let conns: Vec<SpvrClientConnPtr> = self.connections
            .lock().await
            .values()
            .cloned()
            .collect();
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
        tracing::info!("alive connections count: {}", self.connections.lock().await.len());
        let conns: Vec<SpvrClientConnPtr> = self.connections
            .lock().await
            .values()
            .cloned()
            .collect();
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
        self.connections.lock().await.len() as u32
    }

    // -- DB --
    pub async fn insert_conn(&self, conn: SpvrClientConnVo) {
        let c_client_conn = gSpvrDatabase
            .lock().await
            .client_conn();
        let _ = c_client_conn
            .lock().await
            .insert_one(conn).await;
    }

    pub async fn update_conn(&self, conn_id: String, timestamp: i64) {
        let c_client_conn = gSpvrDatabase
            .lock().await
            .client_conn();

        let filter = doc! {
            "conn_id": conn_id,
        };
        let update = doc! {
            "$set" : doc! {
                "last_update_timestamp": timestamp,
                "readable_update_ts": gr_base::format_readable_timestamp(timestamp),
            }
        };
        let _ = c_client_conn
            .lock().await
            .update_one(filter, update).await;
    }

    pub async fn query_client_conns(&self, device_id: String, page: i32, page_size: i32) -> Result<Vec<SpvrClientConnVo>, SpvrApiError> {
        let skip = (page-1) * page_size;
        let limit = page_size as i64;

        let r = gSpvrDatabase
            .lock().await
            .client_conn()
            .lock().await
            .find(doc!{
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

    pub async fn query_conns(&self, page: i32, page_size: i32) -> Result<Vec<SpvrClientConnVo>, SpvrApiError> {
        let skip = (page-1) * page_size;
        let limit = page_size as i64;

        let r = gSpvrDatabase
            .lock().await
            .client_conn()
            .lock().await
            .find(doc!{})
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