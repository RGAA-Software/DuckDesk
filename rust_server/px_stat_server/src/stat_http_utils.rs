use crate::stat_api_error::StatApiError;
use axum::body::Body;
use tokio_stream::StreamExt;

pub async fn get_body(body: Body) -> Result<String, StatApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| StatApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let r = String::from_utf8(bytes).map_err(|_| StatApiError::InvalidParams)?;
    if r.is_empty() {
        return Err(StatApiError::InvalidParams);
    }
    Ok(r)
}

pub fn get_body_str(body: &serde_json::Value, key: &str) -> Result<String, StatApiError> {
    let v = body
        .get(key)
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

