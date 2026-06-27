use crate::event::spvr_event::SpvrEvent;
use crate::event::spvr_event_keys::{
    EVENT_CPU, EVENT_DISK, EVENT_GPU, EVENT_MEMORY, EVENT_TYPE, KEY_CPU_USAGE, KEY_DISK_PATH,
    KEY_DISK_USAGE, KEY_GPU_ID, KEY_GPU_NAME, KEY_GPU_USAGE, KEY_MEMORY_USAGE,
};
use crate::relay::relay_message::{KEY_DEVICE_NAME, KEY_PAGE_SIZE};
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::{KEY_DEVICE_ID, KEY_DEVICE_IP};
use crate::spvr_http_util::{
    get_body, get_body_int, get_body_str_or_empty, get_int_param, get_str_param_or,
};
use crate::user::spvr_user_keys::{
    KEY_FILE, KEY_PAGE, KEY_USER_ID, KEY_USER_NAME,
};
use crate::{gDeviceManager, gSpvrEventMgr, gSpvrSettings, gUserManager};
use axum::body::Body;
use axum::extract::{Multipart, Query, State};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use mongodb::bson::Bson;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;

pub async fn handle_add_event(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrEvent>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let event_type = get_body_str_or_empty(&r, EVENT_TYPE);
    let device_id = get_body_str_or_empty(&r, KEY_DEVICE_ID);
    let device_ip = get_body_str_or_empty(&r, KEY_DEVICE_IP);
    let device_name = get_body_str_or_empty(&r, KEY_DEVICE_NAME);
    let uid = get_body_str_or_empty(&r, KEY_USER_ID);
    let username = get_body_str_or_empty(&r, KEY_USER_NAME);

    // cpu
    if event_type == EVENT_CPU {
        let cpu_usage = get_body_int(&r, KEY_CPU_USAGE)?;
        let event = SpvrEvent::new_cpu(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            cpu_usage as u32,
        );
        let _r = gSpvrEventMgr.add_event(event.clone()).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_MEMORY {
        let mem_usage = get_body_int(&r, KEY_MEMORY_USAGE)?;
        let event = SpvrEvent::new_memory(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            mem_usage as u32,
        );
        let _r = gSpvrEventMgr.add_event(event.clone()).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_DISK {
        let disk_usage = get_body_int(&r, KEY_DISK_USAGE)?;
        let disk_path = get_body_str_or_empty(&r, KEY_DISK_PATH);
        let event = SpvrEvent::new_disk(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            disk_usage as u32,
            disk_path,
        );
        let _r = gSpvrEventMgr.add_event(event.clone()).await?;
        return Ok(Json(ok_resp(event)));
    } else if event_type == EVENT_GPU {
        let gpu_usage = get_body_int(&r, KEY_GPU_USAGE)?;
        let gpu_id = get_body_str_or_empty(&r, KEY_GPU_ID);
        let gpu_name = get_body_str_or_empty(&r, KEY_GPU_NAME);
        let event = SpvrEvent::new_gpu(
            device_id,
            device_ip,
            device_name,
            uid,
            username,
            gpu_usage as u32,
            gpu_id,
            gpu_name,
        );
        let _r = gSpvrEventMgr.add_event(event.clone()).await?;
        return Ok(Json(ok_resp(event)));
    }
    Ok(Json(ok_resp(SpvrEvent::default())))
}

pub async fn handle_remove_event(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    _b: Body,
) -> Result<Json<RespMessage<SpvrEvent>>, SpvrApiError> {
    Ok(Json(ok_resp(SpvrEvent::default())))
}

pub async fn handle_query_events(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<SpvrEvent>>>, SpvrApiError> {
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
    let r = gSpvrEventMgr
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
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u64>>, SpvrApiError> {
    let event_type = get_str_param_or(&query, EVENT_TYPE, "")?;
    let r = gSpvrEventMgr.count_total_events(event_type).await?;
    Ok(Json(ok_resp(r)))
}

pub async fn handle_add_log(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
    mut multipart: Multipart,
) -> Result<Json<RespMessage<SpvrEvent>>, SpvrApiError> {
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
        .map_err(|_e| SpvrApiError::InvalidParams)?
    {
        let key = field.name().unwrap_or("").to_string();
        let filename = field.file_name().unwrap_or("").to_string();
        tracing::info!("log key: {} filename: {}", key, filename);
        if key == KEY_FILE {
            // copy file
            let current_ts = gr_base::get_current_timestamp().to_string();
            let target_name = format!("{}_{}_{}.{}", uid, device_id, current_ts, filename);
            let logs_path = gSpvrSettings.lock().await.upload_logs_path.clone();
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
                        return Err(SpvrApiError::UploadFileFailed);
                    }
                }
            }
            break;
        }
    }

    target_log_path.replace_range(0..1, "");
    let event = SpvrEvent::new_analyze_log(uid, user.username, device_id, target_log_path);

    gSpvrEventMgr.add_event(event.clone()).await?;

    Ok(Json(ok_resp(event)))
}
