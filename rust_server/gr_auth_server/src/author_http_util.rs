use crate::author_api_error::AuthorApiError;
use axum::body::Body;
use axum::extract::Query;
use futures_util::StreamExt;
use std::collections::HashMap;

pub async fn get_body(body: Body) -> Result<String, AuthorApiError> {
    let mut bytes = Vec::new();
    let mut body_stream = body.into_data_stream();
    while let Some(chunk) = body_stream.next().await {
        let chunk = chunk.map_err(|_| AuthorApiError::InvalidParams)?;
        bytes.extend_from_slice(&chunk);
    }
    let r = String::from_utf8(bytes).map_err(|_| AuthorApiError::InvalidParams)?;
    if r.is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(r)
}

pub fn get_body_str(body: &serde_json::Value, key: &str) -> Result<String, AuthorApiError> {
    let v = body
        .get(key)
        .and_then(|v| v.as_str())
        .filter(|s| !s.is_empty())
        .ok_or(AuthorApiError::InvalidParams)?
        .to_string();
    Ok(v)
}

pub fn get_body_int(body: &serde_json::Value, key: &str) -> Result<i64, AuthorApiError> {
    Ok(body
        .get(key)
        .and_then(|v| v.as_i64())
        .ok_or(AuthorApiError::InvalidParams)?)
}

pub fn get_str_param(
    query: &Query<HashMap<String, String>>,
    key: &str,
) -> Result<String, AuthorApiError> {
    query
        .get(key)
        .filter(|s| !s.is_empty())
        .cloned()
        .ok_or(AuthorApiError::InvalidParams)
}

pub fn get_int_param(query: &HashMap<String, String>, key: &str) -> Result<i32, AuthorApiError> {
    query
        .get(key)
        .filter(|s| !s.is_empty()) // 先确保有值且非空
        .ok_or(AuthorApiError::InvalidParams)?
        .parse::<i32>() // 尝试解析为 i32
        .map_err(|_| AuthorApiError::InvalidParams)
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::body::Bytes;
    use serde_json::json;

    #[tokio::test]
    async fn get_body_rejects_empty_body() {
        let err = get_body(Body::empty())
            .await
            .expect_err("empty body should fail");

        assert_eq!(
            err.business_code(),
            AuthorApiError::InvalidParams.business_code()
        );
    }

    #[tokio::test]
    async fn get_body_rejects_invalid_utf8() {
        let body = Body::from(Bytes::from_static(&[0xff, 0xfe, 0xfd]));

        let err = get_body(body).await.expect_err("invalid utf8 should fail");

        assert_eq!(
            err.business_code(),
            AuthorApiError::InvalidParams.business_code()
        );
    }

    #[test]
    fn get_body_str_requires_present_non_empty_string() {
        let body = json!({
            "name": "Admin",
            "empty": "",
            "number": 1,
        });

        assert_eq!(get_body_str(&body, "name").unwrap(), "Admin");
        assert!(get_body_str(&body, "empty").is_err());
        assert!(get_body_str(&body, "number").is_err());
        assert!(get_body_str(&body, "missing").is_err());
    }

    #[test]
    fn get_body_int_requires_integer() {
        let body = json!({
            "days": 30,
            "text": "30",
            "float": 1.5,
        });

        assert_eq!(get_body_int(&body, "days").unwrap(), 30);
        assert!(get_body_int(&body, "text").is_err());
        assert!(get_body_int(&body, "float").is_err());
        assert!(get_body_int(&body, "missing").is_err());
    }

    #[test]
    fn get_str_param_rejects_missing_and_empty_values() {
        let mut map = HashMap::new();
        map.insert("name".to_string(), "Admin".to_string());
        map.insert("empty".to_string(), "".to_string());
        let query = Query(map);

        assert_eq!(get_str_param(&query, "name").unwrap(), "Admin");
        assert!(get_str_param(&query, "empty").is_err());
        assert!(get_str_param(&query, "missing").is_err());
    }

    #[test]
    fn get_int_param_rejects_missing_empty_and_non_numeric_values() {
        let mut map = HashMap::new();
        map.insert("page".to_string(), "1".to_string());
        map.insert("empty".to_string(), "".to_string());
        map.insert("text".to_string(), "abc".to_string());

        assert_eq!(get_int_param(&map, "page").unwrap(), 1);
        assert!(get_int_param(&map, "empty").is_err());
        assert!(get_int_param(&map, "text").is_err());
        assert!(get_int_param(&map, "missing").is_err());
    }
}
