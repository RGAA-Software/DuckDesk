use crate::gRecordFileTransferManager;
use crate::gRecordVisitManager;
use crate::record::spvr_file_transfer::{SpvrFileTransfer, SpvrUpdateFileTransfer};
use crate::record::spvr_visit::{SpvrUpdateVisit, SpvrVisit};
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_http_util::{
    get_int_param, get_int_param_or, get_str_param_allow_empty,
};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;
pub async fn handle_hello_world(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    Ok(Json(ok_resp("hello world".to_string())))
}

pub async fn handle_upload_visit_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(mut up_visit): Json<SpvrVisit>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    up_visit.created_timestamp = gr_base::get_current_timestamp();
    tracing::info!("upload_visit_info : {:?}", up_visit);
    let _info = gRecordVisitManager.insert_visit_info(up_visit).await?;
    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_update_visit_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(update_visit): Json<SpvrUpdateVisit>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    tracing::info!("update_visit_info : {:?}", update_visit);
    let _info = gRecordVisitManager.update_visit_info(update_visit).await?;
    Ok(Json(ok_resp("update ok".to_string())))
}

pub async fn handle_query_update_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<Vec<SpvrVisit>>>, SpvrApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let visit_device_id = get_str_param_allow_empty(&query, "visit_device_id")?;
    let target_device_id = get_str_param_allow_empty(&query, "target_device_id")?;
    let total_size = gRecordVisitManager.total_size().await?;
    let mut r = gRecordVisitManager
        .query_info::<String>(
            page,
            page_size,
            HashMap::default(),
            Some(String::from("created_timestamp")),
            Some(sort_time),
            if visit_device_id.is_empty() {
                None
            } else {
                Some(visit_device_id)
            },
            if target_device_id.is_empty() {
                None
            } else {
                Some(target_device_id)
            },
        )
        .await?;
    for visit in &mut r {
        visit.total = total_size;
    }
    Ok(Json(ok_resp(r)))
}

pub async fn handle_upload_file_transfer_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(mut up_file_transfer): Json<SpvrFileTransfer>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    up_file_transfer.created_timestamp = gr_base::get_current_timestamp();
    tracing::info!("upload_file_transfer : {:?}", up_file_transfer);
    let _info = gRecordFileTransferManager
        .insert_file_transfer_info(up_file_transfer)
        .await?;
    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_update_file_transfer_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(up_file_transfer): Json<SpvrUpdateFileTransfer>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    tracing::info!("update_file_transfer : {:?}", up_file_transfer);
    let _info = gRecordFileTransferManager
        .update_file_transfer_info(up_file_transfer)
        .await?;
    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_query_file_transfer_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<Vec<SpvrFileTransfer>>>, SpvrApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let visit_device_id = get_str_param_allow_empty(&query, "visit_device_id")?;
    let target_device_id = get_str_param_allow_empty(&query, "target_device_id")?;
    let total_size = gRecordFileTransferManager.total_size().await?;
    let mut r = gRecordFileTransferManager
        .query_info::<String>(
            page,
            page_size,
            HashMap::default(),
            Some(String::from("created_timestamp")),
            Some(sort_time),
            if visit_device_id.is_empty() {
                None
            } else {
                Some(visit_device_id)
            },
            if target_device_id.is_empty() {
                None
            } else {
                Some(target_device_id)
            },
        )
        .await?;
    for ft in &mut r {
        ft.total = total_size;
    }
    Ok(Json(ok_resp(r)))
}
