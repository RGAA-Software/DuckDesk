use std::collections::HashMap;
use axum::body::Body;
use axum::extract::Query;
use futures_util::StreamExt;
use crate::update_api_error::UpdateApiError;

pub async fn get_body_str(body: Body) -> Result<String, UpdateApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| UpdateApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let r = String::from_utf8(bytes)
        .map_err(|_| UpdateApiError::InvalidParams)?;
    if r.is_empty() {
        return Err(UpdateApiError::InvalidParams);
    }
    Ok(r)
}

pub fn get_str_param(query: &Query<HashMap<String, String>>, key: &str) -> Result<String, UpdateApiError> {
    query.get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .ok_or(UpdateApiError::InvalidParams)
}

pub fn get_str_param_or(query: &Query<HashMap<String, String>>, key: &str, def: &str) -> Result<String, UpdateApiError> {
    let r = query.get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .unwrap_or(def.to_string());
    Ok(r)
}

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, UpdateApiError> {
    query.get(key)
        .filter(|s| !s.is_empty())          // 先确保有值且非空
        .ok_or(UpdateApiError::InvalidParams)?
        .parse::<i32>()                     // 尝试解析为 i32
        .map_err(|_| UpdateApiError::InvalidParams)
}

pub fn get_int_param_or(query: &HashMap<String, String>, key: &str, def: i32) -> Result<i32, UpdateApiError> {
    let r = query.get(key)
        .filter(|s| !s.is_empty())          // 先确保有值且非空
        .ok_or(UpdateApiError::InvalidParams)?
        .parse::<i32>()                     // 尝试解析为 i32
        .unwrap_or(def);
    Ok(r)
}