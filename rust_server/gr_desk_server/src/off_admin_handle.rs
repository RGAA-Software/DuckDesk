use crate::gOffSettings;
use crate::off_api_error::OffApiError;
use crate::off_http_utils::get_body;
use axum::body::Body;
use axum::http::HeaderMap;
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use serde_json::Value;

pub const HEADER_ADMIN_TOKEN: &str = "x-admin-token";

/// 校验管理接口请求头中的 X-Admin-Token
pub fn check_admin_token(headers: &HeaderMap) -> Result<(), OffApiError> {
    let token = headers
        .get(HEADER_ADMIN_TOKEN)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    let expected = gOffSettings.admin_password();
    if !token.is_empty() && !expected.is_empty() && token == expected {
        Ok(())
    } else {
        Err(OffApiError::Unauthorized)
    }
}

/// 管理后台登录：校验密码是否正确
pub async fn handle_admin_verify(body: Body) -> Result<Json<RespMessage<String>>, OffApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).map_err(|_| OffApiError::InvalidParams)?;
    let password = r["password"].as_str().unwrap_or("");
    let expected = gOffSettings.admin_password();
    if !password.is_empty() && !expected.is_empty() && password == expected {
        Ok(Json(ok_resp("".to_string())))
    } else {
        Err(OffApiError::Unauthorized)
    }
}
