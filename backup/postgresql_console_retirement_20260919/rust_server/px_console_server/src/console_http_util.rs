use crate::console_api_error::ConsoleApiError;
use axum::body::Body;
use axum::extract::Query;
use std::collections::HashMap;
use tokio_stream::StreamExt;

pub async fn get_body(body: Body) -> Result<String, ConsoleApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| ConsoleApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let body_text = String::from_utf8(bytes).map_err(|_| ConsoleApiError::InvalidParams)?;
    if body_text.is_empty() {
        return Err(ConsoleApiError::InvalidParams);
    }
    Ok(body_text)
}

pub fn get_body_str(body: &serde_json::Value, key: &str) -> Result<String, ConsoleApiError> {
    let string_value = body
        .get(key)
        .and_then(|field_value| field_value.as_str())
        .filter(|field_text| !field_text.is_empty())
        .ok_or(ConsoleApiError::InvalidParams)?
        .to_string();
    Ok(string_value)
}

pub fn get_body_str_or_empty(body: &serde_json::Value, key: &str) -> String {
    body.get(key)
        .and_then(|field_value| field_value.as_str())
        .filter(|field_text| !field_text.is_empty())
        .unwrap_or("")
        .to_string()
}

pub fn get_body_bool(body: &serde_json::Value, key: &str) -> Result<bool, ConsoleApiError> {
    let boolean_value = body
        .get(key)
        .and_then(|field_value| field_value.as_bool())
        .ok_or(ConsoleApiError::InvalidParams)?;
    Ok(boolean_value)
}

pub fn get_body_int(body: &serde_json::Value, key: &str) -> Result<i64, ConsoleApiError> {
    let integer_value = body
        .get(key)
        .and_then(|field_value| field_value.as_i64())
        .ok_or(ConsoleApiError::InvalidParams)?;
    Ok(integer_value)
}

//////// Query /////////

pub fn get_str_param(
    query: &Query<HashMap<String, String>>,
    key: &str,
) -> Result<String, ConsoleApiError> {
    query
        .get(key)
        .filter(|parameter_value| !parameter_value.is_empty())
        .cloned()
        .ok_or(ConsoleApiError::InvalidParams)
}

pub fn get_str_param_or(
    query: &Query<HashMap<String, String>>,
    key: &str,
    def: &str,
) -> Result<String, ConsoleApiError> {
    if let Ok(parameter_value) = query
        .get(key)
        .filter(|parameter_value| !parameter_value.is_empty())
        .cloned()
        .ok_or(ConsoleApiError::InvalidParams)
    {
        Ok(parameter_value)
    } else {
        Ok(def.to_string())
    }
}

// allow to get an empty string
pub fn get_str_param_allow_empty(
    query: &Query<HashMap<String, String>>,
    key: &str,
) -> Result<String, ConsoleApiError> {
    query
        .get(key)
        .cloned()
        .ok_or(ConsoleApiError::InvalidParams)
}

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, ConsoleApiError> {
    query
        .get(key)
        .filter(|parameter_value| !parameter_value.is_empty()) // 先确保有值且非空
        .ok_or(ConsoleApiError::InvalidParams)?
        .parse::<i32>() // 尝试解析为 i32
        .map_err(|_| ConsoleApiError::InvalidParams)
}

pub fn get_int_param_or(
    query: &HashMap<String, String>,
    key: &str,
    def: i32,
) -> Result<i32, ConsoleApiError> {
    let parameter_value = query.get(key);
    if let Some(parameter_value) = parameter_value {
        if parameter_value.is_empty() {
            Ok(def)
        } else {
            Ok(parameter_value.parse::<i32>().unwrap_or(def))
        }
    } else {
        Ok(def)
    }
}
