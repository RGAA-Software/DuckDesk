use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::config::cms_access_info::CmsAccessInfo;
use crate::config::cms_server_config::CmsServerConfig;
use crate::{gAuthManager, gCmsClientConnMgr, gCmsContext, gCmsSettings, gCmsSystemMgr};
use axum::extract::State;
use axum::Json;
use px_base::RespMessage;
use serde::{Deserialize, Serialize};
use std::default::Default;
use std::sync::Arc;
use tokio::sync::Mutex;

// pub async fn get_online_relay_servers(State(_ctx): State<Arc<Mutex<CmsContext>>>)
//                                          -> Result<Json<RespMessage<Vec<CmsServerConfig>>>, CmsApiError> {
//     let mut result: Vec<CmsServerConfig> = Default::default();
//     let relay_conn = gCmsInnerConnMgr
//         .lock().await
//         .get_relay_conn().await;
//     if let Some(relay_conn) = relay_conn {
//         result.push(relay_conn.lock().await.get_server_info().await);
//     }
//     Ok(Json(px_base::ok_resp(result)))
// }

pub async fn get_servers_config(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<Vec<CmsServerConfig>>>, CmsApiError> {
    let mut result: Vec<CmsServerConfig> = Default::default();
    // cms / relay
    let config = gCmsSettings.lock().await.get_server_config().await;
    result.push(config);

    Ok(Json(px_base::ok_resp(result)))
}

pub async fn gen_access_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    if let Ok(info) = gCmsContext.lock().await.get_encrypt_access_info().await {
        Ok(Json(px_base::ok_resp(info)))
    } else {
        Err(CmsApiError::InternalError)
    }
}

pub async fn gen_raw_access_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<CmsAccessInfo>>, CmsApiError> {
    let info = gCmsContext.lock().await.gen_access_info().await;
    Ok(Json(px_base::ok_resp(info)))
}

pub async fn handle_get_machine_code(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    let mc = gCmsContext.lock().await.machine_code.clone();
    Ok(Json(px_base::ok_resp(mc)))
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct CachedDataSize {
    pub size: i64,
    pub readable_size: String,
}

pub async fn gen_cached_data_size(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<CachedDataSize>>, CmsApiError> {
    let data_size = gCmsSystemMgr.cal_data_size();
    let readable_data_size = px_base::format_file_size(data_size);

    Ok(Json(px_base::ok_resp(CachedDataSize {
        size: data_size,
        readable_size: readable_data_size,
    })))
}

pub async fn clear_cached_data(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<CachedDataSize>>, CmsApiError> {
    gCmsSystemMgr.clear_data().await;

    let data_size = gCmsSystemMgr.cal_data_size();
    let readable_data_size = px_base::format_file_size(data_size);

    Ok(Json(px_base::ok_resp(CachedDataSize {
        size: data_size,
        readable_size: readable_data_size,
    })))
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct AliveConnections {
    pub total: u32,
    pub relay: u32,
}

pub async fn query_alive_connections_count(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<AliveConnections>>, CmsApiError> {
    let relay_count = 0;
    let client_conns = gCmsClientConnMgr.get_alive_connections_ptr().await;
    let clients_count = client_conns.len() as u32;

    Ok(Json(px_base::ok_resp(AliveConnections {
        total: clients_count,
        relay: relay_count,
    })))
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct AvailableNewConnection {
    pub available: bool,
}
pub async fn query_available_new_connection(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<AvailableNewConnection>>, CmsApiError> {
    let client_conns = gCmsClientConnMgr.get_alive_connections_ptr().await;
    let clients_count = client_conns.len() as u32;

    let auth = gAuthManager.lock().await.get_auth().await;

    let available = clients_count < auth.max_streams as u32;
    tracing::info!(
        "available connections: {}, clients_count: {}, max: {}",
        available,
        clients_count,
        auth.max_streams
    );
    Ok(Json(px_base::ok_resp(AvailableNewConnection { available })))
}
