use crate::off_api_error::OffApiError;
use axum::body::Body;
use std::collections::HashMap;
use tokio_stream::StreamExt;

pub async fn get_body(body: Body) -> Result<String, OffApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| OffApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let r = String::from_utf8(bytes).map_err(|_| OffApiError::InvalidParams)?;
    if r.is_empty() {
        return Err(OffApiError::InvalidParams);
    }
    Ok(r)
}

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, OffApiError> {
    query
        .get(key)
        .filter(|s| !s.is_empty()) // 先确保有值且非空
        .ok_or(OffApiError::InvalidParams)?
        .parse::<i32>() // 尝试解析为 i32
        .map_err(|_| OffApiError::InvalidParams)
}

pub fn get_int_param_or(
    query: &HashMap<String, String>,
    key: &str,
    def: i32,
) -> Result<i32, OffApiError> {
    let r = query
        .get(key)
        .filter(|s| !s.is_empty()) // 先确保有值且非空
        .ok_or(OffApiError::InvalidParams)?
        .parse::<i32>() // 尝试解析为 i32
        .unwrap_or(def);
    Ok(r)
}
