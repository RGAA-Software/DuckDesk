use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::cms_defs::{KEY_DEVICE_ID, KEY_DEVICE_IP};
use crate::cms_http_util::{
    get_body, get_body_int, get_body_str_or_empty, get_int_param, get_str_param_or,
};
use crate::cms_relay::relay_message::{KEY_DEVICE_NAME, KEY_PAGE_SIZE};
use crate::event::cms_event::CmsEvent;
use crate::event::cms_event_keys::{
    EVENT_CPU, EVENT_DISK, EVENT_GPU, EVENT_MEMORY, EVENT_TYPE, KEY_CPU_USAGE, KEY_DISK_PATH,
    KEY_DISK_USAGE, KEY_GPU_ID, KEY_GPU_NAME, KEY_GPU_USAGE, KEY_MEMORY_USAGE,
};
use crate::user::cms_user_keys::{KEY_FILE, KEY_PAGE, KEY_USER_ID, KEY_USER_NAME};
use crate::{gCmsEventMgr, gCmsSettings, gDeviceManager, gUserManager};
use axum::body::Body;
use axum::extract::{Multipart, Query, State};
use axum::Json;
use mongodb::bson::Bson;
use px_base::{ok_resp, RespMessage};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;

pub async fn handle_add_event(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    b: Body,
) -> Result<Json<RespMessage<CmsEvent>>, CmsApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).map_err(|error| {
        tracing::warn!("invalid event request body: {}", error);
        CmsApiError::InvalidParams
    })?;
    let event_type = get_body_str_or_empty(&r, EVENT_TYPE);
    let device_id = get_body_str_or_empty(&r, KEY_DEVICE_ID);
    let device_ip = get_body_str_or_empty(&r, KEY_DEVICE_IP);
    let device_name = get_body_str_or_empty(&r, KEY_DEVICE_NAME);
    let uid = get_body_str_or_empty(&r, KEY_USER_ID);
    let mut username = get_body_str_or_empty(&r, KEY_USER_NAME);
    // Older panel clients used `user_name` while the CMS API uses `username`.
    // Accept both so telemetry does not silently lose its reporting user.
    if username.is_empty() {
        username = get_body_str_or_empty(&r, "user_name");
    }

    if device_id.trim().is_empty() {
        return Err(CmsApiError::InvalidParams);
    }

    // cpu
    if event_type == EVENT_CPU {
        let cpu_usage = validate_usage(get_body_int(&r, KEY_CPU_USAGE)?)?;
        let event = CmsEvent::new_cpu(device_id, device_ip, device_name, uid, username, cpu_usage);
        let event = gCmsEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_MEMORY {
        let mem_usage = validate_usage(get_body_int(&r, KEY_MEMORY_USAGE)?)?;
        let event =
            CmsEvent::new_memory(device_id, device_ip, device_name, uid, username, mem_usage);
        let event = gCmsEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_DISK {
        let disk_usage = validate_usage(get_body_int(&r, KEY_DISK_USAGE)?)?;
        let disk_path = get_body_str_or_empty(&r, KEY_DISK_PATH);
        if disk_path.trim().is_empty() {
            return Err(CmsApiError::InvalidParams);
        }
        let event = CmsEvent::new_disk(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            disk_usage,
            disk_path,
        );
        let event = gCmsEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_GPU {
        let gpu_usage = validate_usage(get_body_int(&r, KEY_GPU_USAGE)?)?;
        let gpu_id = get_body_str_or_empty(&r, KEY_GPU_ID);
        let gpu_name = get_body_str_or_empty(&r, KEY_GPU_NAME);
        let event = CmsEvent::new_gpu(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            gpu_usage,
            gpu_id,
            gpu_name,
        );
        let event = gCmsEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    }
    Err(CmsApiError::InvalidParams)
}

fn validate_usage(value: i64) -> Result<u32, CmsApiError> {
    if !(0..=100).contains(&value) {
        return Err(CmsApiError::InvalidParams);
    }
    Ok(value as u32)
}

#[cfg(test)]
mod tests {
    use super::validate_usage;

    #[test]
    fn usage_must_be_a_percentage() {
        assert_eq!(validate_usage(0).unwrap(), 0);
        assert_eq!(validate_usage(100).unwrap(), 100);
        assert!(validate_usage(-1).is_err());
        assert!(validate_usage(101).is_err());
    }
}

pub async fn handle_remove_event(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    _b: Body,
) -> Result<Json<RespMessage<CmsEvent>>, CmsApiError> {
    Ok(Json(ok_resp(CmsEvent::default())))
}

pub async fn handle_query_events(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<CmsEvent>>>, CmsApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;
    let event_type = get_str_param_or(&query, EVENT_TYPE, "")?;
    let device_id = get_str_param_or(&query, KEY_DEVICE_ID, "")?;
    let device_name = get_str_param_or(&query, KEY_DEVICE_NAME, "")?;
    let device_ip = get_str_param_or(&query, KEY_DEVICE_IP, "")?;

    let mut filters: HashMap<String, Bson> = HashMap::new();
    if !event_type.is_empty() {
        filters.insert(EVENT_TYPE.to_string(), Bson::String(event_type));
    }
    if !device_id.is_empty() {
        filters.insert(KEY_DEVICE_ID.to_string(), Bson::String(device_id));
    }
    if !device_name.is_empty() {
        filters.insert(KEY_DEVICE_NAME.to_string(), Bson::String(device_name));
    }
    if !device_ip.is_empty() {
        filters.insert(KEY_DEVICE_IP.to_string(), Bson::String(device_ip));
    }
    let r = gCmsEventMgr
        .query_events(
            page,
            page_size,
            filters,
            Some("timestamp".to_string()),
            Some(-1),
        )
        .await?;

    Ok(Json(ok_resp(r)))
}

pub async fn handle_count_events(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u64>>, CmsApiError> {
    let event_type = get_str_param_or(&query, EVENT_TYPE, "")?;
    let r = gCmsEventMgr.count_total_events(event_type).await?;
    Ok(Json(ok_resp(r)))
}

pub async fn handle_add_log(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
    mut multipart: Multipart,
) -> Result<Json<RespMessage<CmsEvent>>, CmsApiError> {
    let uid = get_str_param_or(&query, KEY_USER_ID, "")?;
    let device_id = get_str_param_or(&query, KEY_DEVICE_ID, "")?;

    tracing::info!("add log, uid: {}, device id: {}", uid, device_id);

    let user = gUserManager.query_user_by_id(uid.clone()).await?;

    let _device = gDeviceManager.query_device_by_id(device_id.clone()).await?;

    tracing::info!("found user to save log, user name: {}", user.username);
    let mut target_log_path = String::new();
    while let Some(mut field) = multipart
        .next_field()
        .await
        .map_err(|_e| CmsApiError::InvalidParams)?
    {
        let key = field.name().unwrap_or("").to_string();
        let filename = field.file_name().unwrap_or("").to_string();
        tracing::info!("log key: {} filename: {}", key, filename);
        if key == KEY_FILE {
            // copy file
            let current_ts = px_base::get_current_timestamp().to_string();
            let target_name = format!("{}_{}_{}.{}", uid, device_id, current_ts, filename);
            let logs_path = gCmsSettings.lock().await.upload_logs_path.clone();
            let target_path = format!("{}/{}", logs_path, target_name);
            target_log_path = target_path.clone();
            tracing::info!("upload avatar file: {}", target_path);
            let mut o_file = tokio::fs::File::create(&target_path).await.unwrap();
            let mut total_size = 0;
            loop {
                match field.chunk().await {
                    Ok(Some(bytes_data)) => {
                        o_file.write_all(&bytes_data).await.unwrap();
                        total_size += bytes_data.len();
                    }
                    Ok(None) => {
                        tracing::info!("upload success: {}", total_size);
                        break;
                    }
                    Err(err) => {
                        tracing::error!("upload avatar field error: {}", err);
                        return Err(CmsApiError::UploadFileFailed);
                    }
                }
            }
            break;
        }
    }

    target_log_path.replace_range(0..1, "");
    let event = CmsEvent::new_analyze_log(uid, user.username, device_id, target_log_path);

    gCmsEventMgr.add_event(event.clone()).await?;

    Ok(Json(ok_resp(event)))
}
