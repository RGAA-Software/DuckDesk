use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::console_defs::{KEY_DEVICE_ID, KEY_DEVICE_IP};
use crate::console_http_util::{
    get_body, get_body_int, get_body_str_or_empty, get_int_param, get_str_param_or,
};
use crate::console_relay::relay_message::{KEY_DEVICE_NAME, KEY_PAGE_SIZE};
use crate::event::console_event::ConsoleEvent;
use crate::event::console_event_keys::{
    EVENT_CPU, EVENT_DISK, EVENT_GPU, EVENT_MEMORY, EVENT_TYPE, KEY_CPU_USAGE, KEY_DISK_PATH,
    KEY_DISK_USAGE, KEY_GPU_ID, KEY_GPU_NAME, KEY_GPU_USAGE, KEY_MEMORY_USAGE,
};
use crate::user::console_user_keys::{KEY_FILE, KEY_PAGE, KEY_USER_ID, KEY_USER_NAME};
use crate::{gConsoleEventMgr, gConsoleSettings, gDeviceManager, gUserManager};
use axum::body::Body;
use axum::extract::{Multipart, Query, State};
use axum::http::HeaderMap;
use axum::Json;
use mongodb::bson::Bson;
use px_base::{ok_resp, RespMessage};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;

pub async fn handle_add_event(
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    headers: HeaderMap,
    b: Body,
) -> Result<Json<RespMessage<ConsoleEvent>>, ConsoleApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).map_err(|error| {
        tracing::warn!("invalid event request body: {}", error);
        ConsoleApiError::InvalidParams
    })?;
    let event_type = get_body_str_or_empty(&r, EVENT_TYPE);
    let device_id = get_body_str_or_empty(&r, KEY_DEVICE_ID);
    let device_ip = get_body_str_or_empty(&r, KEY_DEVICE_IP);
    let device_name = get_body_str_or_empty(&r, KEY_DEVICE_NAME);
    let uid = get_body_str_or_empty(&r, KEY_USER_ID);
    let mut username = get_body_str_or_empty(&r, KEY_USER_NAME);
    // Older panel clients used `user_name` while the Console API uses `username`.
    // Accept both so telemetry does not silently lose its reporting user.
    if username.is_empty() {
        username = get_body_str_or_empty(&r, "user_name");
    }

    if device_id.trim().is_empty() {
        return Err(ConsoleApiError::InvalidParams);
    }
    validate_event_reporter(&headers, &device_id).await?;

    // cpu
    if event_type == EVENT_CPU {
        let cpu_usage = validate_usage(get_body_int(&r, KEY_CPU_USAGE)?)?;
        let event = ConsoleEvent::new_cpu(device_id, device_ip, device_name, uid, username, cpu_usage);
        let event = gConsoleEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_MEMORY {
        let mem_usage = validate_usage(get_body_int(&r, KEY_MEMORY_USAGE)?)?;
        let event =
            ConsoleEvent::new_memory(device_id, device_ip, device_name, uid, username, mem_usage);
        let event = gConsoleEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_DISK {
        let disk_usage = validate_usage(get_body_int(&r, KEY_DISK_USAGE)?)?;
        let disk_path = get_body_str_or_empty(&r, KEY_DISK_PATH);
        if disk_path.trim().is_empty() {
            return Err(ConsoleApiError::InvalidParams);
        }
        let event = ConsoleEvent::new_disk(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            disk_usage,
            disk_path,
        );
        let event = gConsoleEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_GPU {
        let gpu_usage = validate_usage(get_body_int(&r, KEY_GPU_USAGE)?)?;
        let gpu_id = get_body_str_or_empty(&r, KEY_GPU_ID);
        let gpu_name = get_body_str_or_empty(&r, KEY_GPU_NAME);
        let event = ConsoleEvent::new_gpu(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            gpu_usage,
            gpu_id,
            gpu_name,
        );
        let event = gConsoleEventMgr.add_or_refresh_telemetry_event(event).await?;
        return Ok(Json(ok_resp(event)));
    }
    Err(ConsoleApiError::InvalidParams)
}

async fn validate_event_reporter(
    headers: &HeaderMap,
    event_device_id: &str,
) -> Result<(), ConsoleApiError> {
    if crate::console_settings::is_auth_bypassed().await {
        return Ok(());
    }
    let reporter = headers
        .get("x-px-device-id")
        .and_then(|value| value.to_str().ok())
        .unwrap_or_default()
        .trim();
    if reporter.is_empty() || reporter.len() > 256 || reporter != event_device_id {
        return Err(ConsoleApiError::Forbidden);
    }
    Ok(())
}

fn validate_usage(value: i64) -> Result<u32, ConsoleApiError> {
    if !(0..=100).contains(&value) {
        return Err(ConsoleApiError::InvalidParams);
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
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    _b: Body,
) -> Result<Json<RespMessage<ConsoleEvent>>, ConsoleApiError> {
    Ok(Json(ok_resp(ConsoleEvent::default())))
}

pub async fn handle_query_events(
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsoleEvent>>>, ConsoleApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;
    let event_type = get_str_param_or(&query, EVENT_TYPE, "")?;
    let device_id = get_str_param_or(&query, KEY_DEVICE_ID, "")?;
    let device_name = get_str_param_or(&query, KEY_DEVICE_NAME, "")?;
    let device_ip = get_str_param_or(&query, KEY_DEVICE_IP, "")?;
    let actor_id = get_str_param_or(&query, "actor_id", "")?;
    let action = get_str_param_or(&query, "action", "")?;
    let result = get_str_param_or(&query, "result", "")?;
    let target_id = get_str_param_or(&query, "target_id", "")?;

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
    for (key, value) in [
        ("actor_id", actor_id),
        ("action", action),
        ("result", result),
        ("target_id", target_id),
    ] {
        if !value.is_empty() {
            filters.insert(key.to_string(), Bson::String(value));
        }
    }
    let r = gConsoleEventMgr
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
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u64>>, ConsoleApiError> {
    let event_type = get_str_param_or(&query, EVENT_TYPE, "")?;
    let r = gConsoleEventMgr.count_total_events(event_type).await?;
    Ok(Json(ok_resp(r)))
}

pub async fn handle_add_log(
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
    mut multipart: Multipart,
) -> Result<Json<RespMessage<ConsoleEvent>>, ConsoleApiError> {
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
        .map_err(|_e| ConsoleApiError::InvalidParams)?
    {
        let key = field.name().unwrap_or("").to_string();
        let filename = field.file_name().unwrap_or("").to_string();
        tracing::info!("log key: {} filename: {}", key, filename);
        if key == KEY_FILE {
            // copy file
            let current_ts = px_base::get_current_timestamp().to_string();
            let target_name = format!("{}_{}_{}.{}", uid, device_id, current_ts, filename);
            let logs_path = gConsoleSettings.lock().await.upload_logs_path.clone();
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
                        return Err(ConsoleApiError::UploadFileFailed);
                    }
                }
            }
            break;
        }
    }

    target_log_path.replace_range(0..1, "");
    let event = ConsoleEvent::new_analyze_log(uid, user.username, device_id, target_log_path);

    gConsoleEventMgr.add_event(event.clone()).await?;

    Ok(Json(ok_resp(event)))
}
