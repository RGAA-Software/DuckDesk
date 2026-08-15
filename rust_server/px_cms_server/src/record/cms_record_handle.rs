use crate::gRecordFileTransferManager;
use crate::gRecordVisitManager;
use crate::record::cms_file_transfer::{CmsFileTransfer, CmsUpdateFileTransfer};
use crate::record::cms_visit::{CmsUpdateVisit, CmsVisit};
use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::cms_http_util::{
    get_int_param, get_int_param_or, get_str_param_allow_empty,
};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

const MAX_PAGE_SIZE: i32 = 1000;

fn validate_paging(page: i32, page_size: i32) -> Result<(), CmsApiError> {
    if page <= 0 || page_size <= 0 || page_size > MAX_PAGE_SIZE {
        return Err(CmsApiError::InvalidParams);
    }
    Ok(())
}

pub async fn handle_hello_world(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    Ok(Json(ok_resp("hello world".to_string())))
}

pub async fn handle_upload_visit_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(mut up_visit): Json<CmsVisit>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    up_visit.created_timestamp = px_base::get_current_timestamp();
    tracing::info!("upload_visit_info : {:?}", up_visit);
    let _info = gRecordVisitManager.insert_visit_info(up_visit).await?;
    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_update_visit_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(update_visit): Json<CmsUpdateVisit>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    tracing::info!("update_visit_info : {:?}", update_visit);
    let _info = gRecordVisitManager.update_visit_info(update_visit).await?;
    Ok(Json(ok_resp("update ok".to_string())))
}

pub async fn handle_query_update_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<Vec<CmsVisit>>>, CmsApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    validate_paging(page, page_size)?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let visit_device_id = get_str_param_allow_empty(&query, "visit_device_id")?;
    let target_device_id = get_str_param_allow_empty(&query, "target_device_id")?;

    let visit_device_filter = if visit_device_id.is_empty() {
        None
    } else {
        Some(visit_device_id.clone())
    };
    let target_device_filter = if target_device_id.is_empty() {
        None
    } else {
        Some(target_device_id.clone())
    };

    let total_size = gRecordVisitManager
        .total_size::<String>(
            HashMap::default(),
            visit_device_filter.clone(),
            target_device_filter.clone(),
        )
        .await?;
    let mut r = gRecordVisitManager
        .query_info::<String>(
            page,
            page_size,
            HashMap::default(),
            Some(String::from("created_timestamp")),
            Some(sort_time),
            visit_device_filter,
            target_device_filter,
        )
        .await?;
    for visit in &mut r {
        visit.total = total_size;
    }
    Ok(Json(ok_resp(r)))
}

pub async fn handle_upload_file_transfer_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(mut up_file_transfer): Json<CmsFileTransfer>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    up_file_transfer.created_timestamp = px_base::get_current_timestamp();
    tracing::info!("upload_file_transfer : {:?}", up_file_transfer);
    let _info = gRecordFileTransferManager
        .insert_file_transfer_info(up_file_transfer)
        .await?;
    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_update_file_transfer_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(up_file_transfer): Json<CmsUpdateFileTransfer>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    tracing::info!("update_file_transfer : {:?}", up_file_transfer);
    let _info = gRecordFileTransferManager
        .update_file_transfer_info(up_file_transfer)
        .await?;
    Ok(Json(ok_resp("update ok".to_string())))
}

pub async fn handle_query_file_transfer_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<Vec<CmsFileTransfer>>>, CmsApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    validate_paging(page, page_size)?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let visit_device_id = get_str_param_allow_empty(&query, "visit_device_id")?;
    let target_device_id = get_str_param_allow_empty(&query, "target_device_id")?;

    let visit_device_filter = if visit_device_id.is_empty() {
        None
    } else {
        Some(visit_device_id.clone())
    };
    let target_device_filter = if target_device_id.is_empty() {
        None
    } else {
        Some(target_device_id.clone())
    };

    let total_size = gRecordFileTransferManager
        .total_size::<String>(
            HashMap::default(),
            visit_device_filter.clone(),
            target_device_filter.clone(),
        )
        .await?;
    let mut r = gRecordFileTransferManager
        .query_info::<String>(
            page,
            page_size,
            HashMap::default(),
            Some(String::from("created_timestamp")),
            Some(sort_time),
            visit_device_filter,
            target_device_filter,
        )
        .await?;
    for ft in &mut r {
        ft.total = total_size;
    }
    Ok(Json(ok_resp(r)))
}
