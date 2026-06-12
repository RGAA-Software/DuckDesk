use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_http_util::{get_body, get_int_param, get_int_param_or, get_str_param, get_str_param_or};
use crate::stream::spvr_stream::SpvrStream;
use crate::stream::spvr_stream_keys::{KEY_STREAM_AUDIO_ENABLED, KEY_STREAM_BG_COLOR, KEY_STREAM_CLIPBOARD_ENABLED, KEY_STREAM_CONNECT_TYPE, KEY_STREAM_CREATED_TIMESTAMP, KEY_STREAM_DESKTOP_NAME, KEY_STREAM_DEVICE_ID, KEY_STREAM_DEVICE_RANDOM_PWD, KEY_STREAM_DEVICE_SAFETY_PWD, KEY_STREAM_HOST, KEY_STREAM_ID, KEY_STREAM_NAME, KEY_STREAM_NETWORK_TYPE, KEY_STREAM_OS_VERSION, KEY_STREAM_PORT, KEY_STREAM_REMOTE_DEVICE_ID, KEY_STREAM_REMOTE_DEVICE_RANDOM_PWD, KEY_STREAM_REMOTE_DEVICE_SAFETY_PWD, KEY_STREAM_SHOW_MAX_WINDOW, KEY_STREAM_SPLIT_WINDOWS, KEY_STREAM_UPDATED_TIMESTAMP};
use crate::{gSpvrStreamMgr};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use mongodb::bson::Bson;
use tokio::sync::Mutex;
use crate::user::spvr_user_keys::{KEY_PAGE, KEY_PAGE_SIZE, KEY_SORT_DIRECTION, KEY_SORT_FIELD};

pub async fn handle_insert_stream(State(_context): State<Arc<Mutex<SpvrContext>>>,
                                  b: Body)
                                  -> Result<Json<RespMessage<SpvrStream>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let mut stream = SpvrStream::default();
    stream.stream_id = r[KEY_STREAM_ID].as_str().unwrap_or("").to_string();
    stream.stream_name = r[KEY_STREAM_NAME].as_str().unwrap_or("").to_string();
    stream.audio_enabled = r[KEY_STREAM_AUDIO_ENABLED].as_bool().unwrap_or(false);
    stream.clipboard_enabled = r[KEY_STREAM_CLIPBOARD_ENABLED].as_bool().unwrap_or(false);
    stream.show_max_window = r[KEY_STREAM_SHOW_MAX_WINDOW].as_bool().unwrap_or(false);
    stream.split_windows = r[KEY_STREAM_SPLIT_WINDOWS].as_bool().unwrap_or(false);
    stream.stream_host = r[KEY_STREAM_HOST].as_str().unwrap_or("").to_string();
    stream.stream_port = r[KEY_STREAM_PORT].as_i64().unwrap_or(0);
    stream.bg_color = r[KEY_STREAM_BG_COLOR].as_i64().unwrap_or(0);
    stream.network_type = r[KEY_STREAM_NETWORK_TYPE].as_str().unwrap_or("").to_string();
    stream.connect_type = r[KEY_STREAM_CONNECT_TYPE].as_str().unwrap_or("").to_string();
    stream.device_id = r[KEY_STREAM_DEVICE_ID].as_str().unwrap_or("").to_string();
    stream.device_random_pwd = r[KEY_STREAM_DEVICE_RANDOM_PWD].as_str().unwrap_or("").to_string();
    stream.device_safety_pwd = r[KEY_STREAM_DEVICE_SAFETY_PWD].as_str().unwrap_or("").to_string();
    stream.remote_device_id = r[KEY_STREAM_REMOTE_DEVICE_ID].as_str().unwrap_or("").to_string();
    stream.remote_device_random_pwd = r[KEY_STREAM_REMOTE_DEVICE_RANDOM_PWD].as_str().unwrap_or("").to_string();
    stream.remote_device_safety_pwd = r[KEY_STREAM_REMOTE_DEVICE_SAFETY_PWD].as_str().unwrap_or("").to_string();
    stream.created_timestamp = r[KEY_STREAM_CREATED_TIMESTAMP].as_i64().unwrap_or(0);
    stream.updated_timestamp = r[KEY_STREAM_UPDATED_TIMESTAMP].as_i64().unwrap_or(0);
    stream.desktop_name = r[KEY_STREAM_DESKTOP_NAME].as_str().unwrap_or("").to_string();
    stream.os_version = r[KEY_STREAM_OS_VERSION].as_str().unwrap_or("").to_string();

    let r = gSpvrStreamMgr
        .query_stream_by_id(stream.stream_id.clone()).await;
     if let Err(e) = r {
         let s = gSpvrStreamMgr
             .insert_stream(stream).await?;
         Ok(Json(ok_resp(s)))
     }
     else {
         let s = gSpvrStreamMgr
             .update_stream(stream).await?;
         Ok(Json(ok_resp(s)))
     }
}

pub async fn handle_delete_stream(State(_context): State<Arc<Mutex<SpvrContext>>>,
                                  b: Body)
                                  -> Result<Json<RespMessage<SpvrStream>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let stream_id = r[KEY_STREAM_ID].as_str().unwrap();
    let s = gSpvrStreamMgr
        .delete_stream(stream_id.to_string()).await?;
    Ok(Json(ok_resp(s)))
}

pub async fn handle_update_stream(State(_context): State<Arc<Mutex<SpvrContext>>>,
                                  b: Body)
                                  -> Result<Json<RespMessage<SpvrStream>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let stream_id = r[KEY_STREAM_ID].as_str().unwrap().to_string();
    let stream = gSpvrStreamMgr
        .query_stream_by_id(stream_id.clone()).await?;
    tracing::info!("found stream {:?} to update.", stream_id);

    let mut update_success = false;
    if let Value::Object(map) = &r {
        for (key, value) in map {
            if key == KEY_STREAM_ID {
                continue;
            }
            match value {
                Value::String(s) => {
                    let value = s.clone();
                    gSpvrStreamMgr
                        .update_stream_field(stream_id.clone(), key.clone(), value).await?;
                    update_success = true;
                },
                Value::Number(n) => {
                    gSpvrStreamMgr
                        .update_stream_field(stream_id.clone(), key.clone(), n.as_i64()).await?;
                    update_success = true;
                },
                Value::Bool(b) => {
                    gSpvrStreamMgr
                        .update_stream_field(stream_id.clone(), key.clone(), b).await?;
                    update_success = true;
                },
                _ => {}
            }
        }
    }

    if update_success {
        let stream = gSpvrStreamMgr
            .query_stream_by_id(stream_id.clone()).await?;
        Ok(Json(ok_resp(stream)))
    }
    else {
        Err(SpvrApiError::UserUpdateFailed)
    }
}

pub async fn handle_query_stream_by_id(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                              query: Query<HashMap<String, String>>)
                              -> Result<Json<RespMessage<SpvrStream>>, SpvrApiError> {
    let stream_id = get_str_param(&query, KEY_STREAM_ID)?;
    let s = gSpvrStreamMgr
        .query_stream_by_id(stream_id).await?;
    Ok(Json(ok_resp(s)))
}

pub async fn handle_query_stream_by_name(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                       query: Query<HashMap<String, String>>)
                                       -> Result<Json<RespMessage<SpvrStream>>, SpvrApiError> {
    let stream_name = get_str_param(&query, KEY_STREAM_NAME)?;
    let s = gSpvrStreamMgr
        .query_stream_by_name(stream_name).await?;
    Ok(Json(ok_resp(s)))
}

pub async fn handle_query_streams(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                       query: Query<HashMap<String, String>>)
                                       -> Result<Json<RespMessage<Vec<SpvrStream>>>, SpvrApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;
    let sort_field = get_str_param_or(&query, KEY_SORT_FIELD, "")?;
    let sort_direction = get_int_param_or(&query, KEY_SORT_DIRECTION, 0)?;
    let key_sort_field = if sort_field.is_empty() { None } else { Some(sort_field) };
    let key_sort_direction = if sort_direction == 0 { None } else { Some(sort_direction) };
    let filters: HashMap<String, Bson> = HashMap::new();
    let streams = gSpvrStreamMgr
        .query_streams(page, page_size, filters, key_sort_field, key_sort_direction).await?;
    Ok(Json(ok_resp(streams)))
}
