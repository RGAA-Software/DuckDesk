use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::cms_http_util::{get_int_param, get_int_param_or, get_str_param_allow_empty};
use crate::gRecordFileTransferManager;
use crate::gRecordVisitManager;
use crate::record::cms_file_transfer::{CmsFileTransfer, CmsUpdateFileTransfer};
use crate::record::cms_visit::{CmsUpdateVisit, CmsVisit};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::http::HeaderMap;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

const MAX_PAGE_SIZE: i32 = 1000;

async fn validate_reporting_device(
    headers: &HeaderMap,
    visitor_device: &str,
    target_device: &str,
) -> Result<(), CmsApiError> {
    let Some(device_id) = reporting_device_id(headers).await? else {
        return Ok(());
    };
    if !reporting_device_matches(&device_id, visitor_device, target_device) {
        tracing::warn!(
            "audit reporter identity mismatch: reporter='{}', visitor='{}', target='{}'",
            &device_id,
            visitor_device,
            target_device
        );
        return Err(CmsApiError::Forbidden);
    }
    Ok(())
}

async fn reporting_device_id(headers: &HeaderMap) -> Result<Option<String>, CmsApiError> {
    if crate::cms_settings::is_auth_bypassed().await {
        return Ok(None);
    }
    let device_id = headers
        .get("x-px-device-id")
        .and_then(|value| value.to_str().ok())
        .unwrap_or_default()
        .trim();
    if device_id.is_empty() || device_id.len() > 256 {
        return Err(CmsApiError::Forbidden);
    }
    Ok(Some(device_id.to_string()))
}

fn reporting_device_matches(device_id: &str, visitor_device: &str, target_device: &str) -> bool {
    device_id == visitor_device || device_id == target_device
}

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
    headers: HeaderMap,
    Json(mut up_visit): Json<CmsVisit>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    validate_reporting_device(&headers, &up_visit.visitor_device, &up_visit.target_device).await?;
    up_visit.created_timestamp = px_base::get_current_timestamp();
    tracing::info!("upload_visit_info : {:?}", up_visit);
    let _info = gRecordVisitManager.insert_visit_info(up_visit).await?;
    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_update_visit_info(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
    headers: HeaderMap,
    Json(update_visit): Json<CmsUpdateVisit>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    let reporter_device = reporting_device_id(&headers).await?;
    tracing::info!("update_visit_info : {:?}", update_visit);
    let _info = gRecordVisitManager
        .update_visit_info(update_visit, reporter_device.as_deref())
        .await?;
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
    headers: HeaderMap,
    Json(mut up_file_transfer): Json<CmsFileTransfer>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    validate_reporting_device(
        &headers,
        &up_file_transfer.visitor_device,
        &up_file_transfer.target_device,
    )
    .await?;
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
    headers: HeaderMap,
    Json(up_file_transfer): Json<CmsUpdateFileTransfer>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    let reporter_device = reporting_device_id(&headers).await?;
    tracing::info!("update_file_transfer : {:?}", up_file_transfer);
    let _info = gRecordFileTransferManager
        .update_file_transfer_info(up_file_transfer, reporter_device.as_deref())
        .await?;
    Ok(Json(ok_resp("update ok".to_string())))
}

#[cfg(test)]
mod reporter_tests {
    use super::*;

    #[test]
    fn reporter_must_be_one_endpoint_of_the_audit_record() {
        assert!(reporting_device_matches("target", "visitor", "target"));
        assert!(reporting_device_matches("visitor", "visitor", "target"));
        assert!(!reporting_device_matches("other", "visitor", "target"));
    }
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
