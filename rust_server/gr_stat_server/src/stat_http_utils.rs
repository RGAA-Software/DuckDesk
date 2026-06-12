use std::collections::HashMap;
use axum::body::Body;
use axum::extract::Query;
use serde::Deserialize;
use tokio_stream::StreamExt;
use crate::stat_api_error::StatApiError;

#[derive(Deserialize)]
struct RequestBody {
    data: String,
}

pub async fn get_body_data(body: Body) -> Result<String, StatApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| StatApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }

    let r = String::from_utf8(bytes)
        .map_err(|_| StatApiError::InvalidParams)?;
    tracing::info!("body: {:#?}", r);
    let rb: RequestBody = serde_json::from_str(r.as_str())
        .map_err(|_| StatApiError::InvalidParams)?;
    if rb.data.is_empty() {
        return Err(StatApiError::InvalidParams);
    }

    Ok(rb.data)
}

pub async fn get_body(body: Body) -> Result<String, StatApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| StatApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let r = String::from_utf8(bytes)
        .map_err(|_| StatApiError::InvalidParams)?;
    if r.is_empty() {
        return Err(StatApiError::InvalidParams);
    }
    Ok(r)
}


pub fn get_body_str(body: &serde_json::Value, key: &str) -> Result<String, StatApiError> {
    let v = body.get(key)
        .and_then(|v| v.as_str())
        .filter(|s| !s.is_empty())
        .ok_or(StatApiError::InvalidParams)?
        .to_string();
    Ok(v)
}

pub fn get_body_str_or_empty(body: &serde_json::Value, key: &str) -> String {
    body.get(key)
        .and_then(|v| v.as_str())
        .filter(|s| !s.is_empty())
        .unwrap_or("")
        .to_string()
}

pub fn get_body_bool(body: &serde_json::Value, key: &str) -> Result<bool, StatApiError> {
    let v = body.get(key)
        .and_then(|v| v.as_bool())
        .ok_or(StatApiError::InvalidParams)?;
    Ok(v)
}

pub fn get_body_int(body: &serde_json::Value, key: &str) -> Result<i64, StatApiError> {
    let v = body.get(key)
        .and_then(|v| v.as_i64())
        .ok_or(StatApiError::InvalidParams)?;
    Ok(v)
}

pub fn get_str_param(query: &Query<HashMap<String, String>>, key: &str) -> Result<String, StatApiError> {
    query.get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .ok_or(StatApiError::InvalidParams)
}

// allow to get an empty string
pub fn get_str_param_allow_empty(query: &Query<HashMap<String, String>>, key: &str) -> Result<String, StatApiError> {
    query.get(key)
        .cloned()
        .ok_or(StatApiError::InvalidParams)
}

pub fn get_str_param_or(query: &Query<HashMap<String, String>>, key: &str, def: &str) -> Result<String, StatApiError> {
    let r = query.get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .unwrap_or(def.to_string());
    Ok(r)
}

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, StatApiError> {
    query.get(key)
        .filter(|s| !s.is_empty())          // 先确保有值且非空
        .ok_or(StatApiError::InvalidParams)?
        .parse::<i32>()                     // 尝试解析为 i32
        .map_err(|_| StatApiError::InvalidParams)
}

pub fn get_int_param_or(query: &HashMap<String, String>, key: &str, def: i32) -> Result<i32, StatApiError> {
    let r = query.get(key)
        .filter(|s| !s.is_empty())          // 先确保有值且非空
        .ok_or(StatApiError::InvalidParams)?
        .parse::<i32>()                     // 尝试解析为 i32
        .unwrap_or(def);
    Ok(r)
}