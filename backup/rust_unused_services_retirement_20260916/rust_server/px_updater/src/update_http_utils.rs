use crate::update_api_error::UpdateApiError;
use std::collections::HashMap;

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, UpdateApiError> {
    query
        .get(key)
        .filter(|s| !s.is_empty()) // 先确保有值且非空
        .ok_or(UpdateApiError::InvalidParams)?
        .parse::<i32>() // 尝试解析为 i32
        .map_err(|_| UpdateApiError::InvalidParams)
}

pub fn get_int_param_or(
    query: &HashMap<String, String>,
    key: &str,
    def: i32,
) -> Result<i32, UpdateApiError> {
    let r = query
        .get(key)
        .filter(|s| !s.is_empty()) // 先确保有值且非空
        .ok_or(UpdateApiError::InvalidParams)?
        .parse::<i32>() // 尝试解析为 i32
        .unwrap_or(def);
    Ok(r)
}
