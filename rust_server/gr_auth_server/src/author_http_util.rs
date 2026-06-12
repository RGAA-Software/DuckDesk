use std::collections::HashMap;
use axum::body::Body;
use axum::extract::Query;
use futures_util::StreamExt;
use serde::Deserialize;
use crate::author_api_error::AuthorApiError;

#[derive(Deserialize)]
struct RequestBody {
    data: String,
}

pub async fn get_body_data(body: Body) -> Result<String, AuthorApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| AuthorApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }

    let r = String::from_utf8(bytes)
        .map_err(|_| AuthorApiError::InvalidParams)?;
    tracing::info!("body: {:#?}", r);
    let rb: RequestBody = serde_json::from_str(r.as_str())
        .map_err(|_| AuthorApiError::InvalidParams)?;
    if rb.data.is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }

    Ok(rb.data)
}

pub async fn get_body(body: Body) -> Result<String, AuthorApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| AuthorApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let r = String::from_utf8(bytes)
        .map_err(|_| AuthorApiError::InvalidParams)?;
    if r.is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(r)
}

pub fn get_body_str(body: &serde_json::Value, key: &str) -> Result<String, AuthorApiError> {
    let v = body.get(key)
        .and_then(|v| v.as_str())
        .filter(|s| !s.is_empty())
        .ok_or(AuthorApiError::InvalidParams)?
        .to_string();
    Ok(v)
}

pub fn get_body_int(body: &serde_json::Value, key: &str) -> Result<i64, AuthorApiError> {
    Ok(body.get(key)
        .and_then(|v| v.as_i64())
        .ok_or(AuthorApiError::InvalidParams)?)
}

pub fn get_str_param(query: &Query<HashMap<String, String>>, key: &str) -> Result<String, AuthorApiError> {
    query.get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .ok_or(AuthorApiError::InvalidParams)
}

// allow to get an empty string
pub fn get_str_param_allow_empty(query: &Query<HashMap<String, String>>, key: &str) -> Result<String, AuthorApiError> {
    query.get(key)
        .cloned()
        .ok_or(AuthorApiError::InvalidParams)
}

pub fn get_str_param_or(query: &Query<HashMap<String, String>>, key: &str, def: &str) -> Result<String, AuthorApiError> {
    let r = query.get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .unwrap_or(def.to_string());
    Ok(r)
}

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, AuthorApiError> {
    query.get(key)
        .filter(|s| !s.is_empty())          // 先确保有值且非空
        .ok_or(AuthorApiError::InvalidParams)?
        .parse::<i32>()                     // 尝试解析为 i32
        .map_err(|_| AuthorApiError::InvalidParams)
}

pub fn get_int_param_or(query: &HashMap<String, String>, key: &str, def: i32) -> Result<i32, AuthorApiError> {
    let r = query.get(key)
        .filter(|s| !s.is_empty())          // 先确保有值且非空
        .ok_or(AuthorApiError::InvalidParams)?
        .parse::<i32>()                     // 尝试解析为 i32
        .unwrap_or(def);
    Ok(r)
}