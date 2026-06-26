use crate::author_api_error::AuthorApiError;
use crate::author_api_error::AuthorApiError::AppkeySecretNotPaired;
use crate::author_http_util::{get_body, get_body_int, get_body_str, get_int_param, get_str_param};
use crate::author_keys::{
    KEY_CREATE_AUTHORIZATION_AUTH_ID, KEY_CREATE_AUTHORIZATION_DAYS,
    KEY_CREATE_AUTHORIZATION_MACHINE_CODE, KEY_CREATE_AUTHORIZATION_MAX_STREAMS,
    KEY_CREATE_AUTHORIZATION_ROLE, KEY_CREATE_AUTHORIZATION_USER_NAME,
};
use crate::author_license_keys::sign_authorization_model;
use crate::authorization_manager::AuthorizationError;
use crate::gAuthorizationManager;
use crate::gLicenseSigner;
use axum::Json;
use axum::body::Body;
use axum::extract::Query;
use gr_auth_mgr::app_secret_util::is_appkey_secret_paired;
use gr_auth_mgr::auth_license::{LicenseVerifier, SignedLicense};
use gr_auth_mgr::authorization::{Authorization, AuthorizationVo};
use gr_base::{RespMessage, ok_resp};
use serde_json::Value;
use std::collections::HashMap;

pub async fn handle_create_new_authorization(
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let input = parse_create_authorization_input(&r)?;

    tracing::info!("customer_role {}", input.role);

    let auth = gAuthorizationManager
        .gen_new_authorization(
            input.name,
            input.machine_code,
            input.days,
            input.max_streams,
            input.role,
        )
        .await;
    match auth {
        Ok(auth) => Ok(Json(ok_resp(auth))),
        Err(e) => match e {
            AuthorizationError::AlreadyExist => Err(AuthorApiError::AlreadyExists),
            AuthorizationError::DatabaseError => Err(AuthorApiError::DatabaseError),
            _ => Err(AuthorApiError::DatabaseError),
        },
    }
}

pub async fn handle_create_new_deploy_authorization(
    body: Body,
) -> Result<Json<RespMessage<String>>, AuthorApiError> {
    let r = handle_create_new_authorization(body).await?;
    let auth = r.0.data;
    let signer_guard = gLicenseSigner.lock().await;
    let signer = signer_guard
        .as_ref()
        .ok_or(AuthorApiError::CantCreateAuthorization)?;
    let signed = sign_authorization_model(signer, &auth)
        .map_err(|_| AuthorApiError::CantCreateAuthorization)?;
    let deploy_str = signed
        .to_deploy_string()
        .map_err(|_| AuthorApiError::CantCreateAuthorization)?;
    Ok(Json(ok_resp(deploy_str)))
}

pub async fn handle_query_authorization_by_id(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let auth_id = get_str_param(&query, "auth_id")?;

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_id(auth_id)
        .await
    {
        Ok(Json(ok_resp(auth)))
    } else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_deploy_authorization_by_id(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<String>>, AuthorApiError> {
    let auth_id = get_str_param(&query, "auth_id")?;

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_id(auth_id)
        .await
    {
        let signer_guard = gLicenseSigner.lock().await;
        let signer = signer_guard
            .as_ref()
            .ok_or(AuthorApiError::CantCreateAuthorization)?;
        let signed = sign_authorization_model(signer, &auth)
            .map_err(|_| AuthorApiError::CantCreateAuthorization)?;
        let deploy_str = signed
            .to_deploy_string()
            .map_err(|_| AuthorApiError::CantCreateAuthorization)?;
        Ok(Json(ok_resp(deploy_str)))
    } else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_authorization_by_name(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let auth_name = get_str_param(&query, "auth_name")?;

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_name(auth_name)
        .await
    {
        Ok(Json(ok_resp(auth)))
    } else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_authorization_like_name(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<AuthorizationVo>>>, AuthorApiError> {
    let auth_name = get_str_param(&query, "auth_name")?;
    if auth_name.is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }

    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;

    if let Ok(auth) = gAuthorizationManager
        .query_authorizations_like_name(auth_name, page, page_size)
        .await
    {
        Ok(Json(ok_resp(auth)))
    } else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_authorizations(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<AuthorizationVo>>>, AuthorApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;

    if let Ok(auth) = gAuthorizationManager
        .query_authorizations_no_filter(page, page_size)
        .await
    {
        Ok(Json(ok_resp(auth)))
    } else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_verify_appkey_secret(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let appkey = get_str_param(&query, "appkey")?;
    let app_secret = get_str_param(&query, "app_secret")?;
    if !is_appkey_secret_paired(appkey.clone(), app_secret.clone()) {
        return Err(AppkeySecretNotPaired);
    }

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_appkey_secret(appkey, app_secret)
        .await
    {
        Ok(Json(ok_resp(auth)))
    } else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_verify_license(
    body: Body,
) -> Result<Json<RespMessage<bool>>, AuthorApiError> {
    let body = get_body(body).await?;
    let value: serde_json::Value =
        serde_json::from_str(&body).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_str = get_body_str(&value, "data")?;

    let signer_guard = gLicenseSigner.lock().await;
    let signer = signer_guard
        .as_ref()
        .ok_or(AuthorApiError::CantCreateAuthorization)?;

    let signed =
        SignedLicense::parse_deploy_string(&auth_str).map_err(|_| AuthorApiError::InvalidParams)?;

    let public_key = signer.public_key_bytes();
    let verifier = LicenseVerifier::from_public_key_bytes(&public_key)
        .map_err(|_| AuthorApiError::DatabaseError)?;
    let now_ms = gr_base::get_current_timestamp();
    let valid = verifier
        .verify(&signed, &signed.license.machine_code, now_ms)
        .map_err(|_| AuthorApiError::DatabaseError)?;
    Ok(Json(ok_resp(valid)))
}

pub async fn handle_update_authorization(
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let input = parse_update_authorization_input(&r)?;
    let auth_id = input.auth_id;
    tracing::info!("Auth ID: {}", auth_id);
    tracing::info!("Days: {}", input.days);
    tracing::info!("Max streams: {}", input.max_streams);
    tracing::info!("Role: {}", input.role);

    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id.clone())
        .await;
    if auth.is_none() {
        return Err(AuthorApiError::AuthorizationNotFound);
    }

    let mut auth = auth.unwrap();
    let current_ts = gr_base::get_current_timestamp();
    auth.days = input.days;
    auth.role = input.role;
    auth.max_streams = input.max_streams;
    auth.created_timestamp_ms = current_ts;
    auth.end_timestamp_ms = current_ts + (input.days as i64) * 24 * 3600 * 1000;
    auth.last_modify_timestamp = current_ts;
    let r = gAuthorizationManager
        .update_authorization(auth_id.clone(), auth)
        .await;
    if r {
        let auth = gAuthorizationManager
            .query_authorization_by_id(auth_id)
            .await
            .unwrap();
        Ok(Json(ok_resp(auth)))
    } else {
        Err(AuthorApiError::UpdateAuthFailed)
    }
}

const MAX_AUTH_NAME_LEN: usize = 128;
const MAX_MACHINE_CODE_LEN: usize = 256;
const MAX_AUTH_DAYS: i32 = 365000;
const MAX_AUTH_STREAMS: i32 = 10000;
const VALID_CUSTOMER_ROLES: std::ops::RangeInclusive<i32> = 1..=3;

#[derive(Debug, PartialEq, Eq)]
struct CreateAuthorizationInput {
    name: String,
    machine_code: String,
    days: i32,
    max_streams: i32,
    role: i32,
}

#[derive(Debug, PartialEq, Eq)]
struct UpdateAuthorizationInput {
    auth_id: String,
    days: i32,
    max_streams: i32,
    role: i32,
}

fn parse_create_authorization_input(
    body: &Value,
) -> Result<CreateAuthorizationInput, AuthorApiError> {
    let name = get_body_str(body, KEY_CREATE_AUTHORIZATION_USER_NAME)?;
    let machine_code = get_body_str(body, KEY_CREATE_AUTHORIZATION_MACHINE_CODE)?;
    let days = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_DAYS)?;
    let max_streams = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)?;
    let role = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_ROLE)?;

    validate_name(&name)?;
    validate_machine_code(&machine_code)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;
    validate_customer_role(role)?;

    Ok(CreateAuthorizationInput {
        name,
        machine_code,
        days,
        max_streams,
        role,
    })
}

fn parse_update_authorization_input(
    body: &Value,
) -> Result<UpdateAuthorizationInput, AuthorApiError> {
    let auth_id = get_body_str(body, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    let days = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_DAYS)?;
    let max_streams = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)?;
    let role = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_ROLE)?;

    validate_auth_id(&auth_id)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;
    validate_customer_role(role)?;

    Ok(UpdateAuthorizationInput {
        auth_id,
        days,
        max_streams,
        role,
    })
}

fn checked_body_i32(body: &Value, key: &str) -> Result<i32, AuthorApiError> {
    i32::try_from(get_body_int(body, key)?).map_err(|_| AuthorApiError::InvalidParams)
}

fn validate_name(name: &str) -> Result<(), AuthorApiError> {
    if name.trim().is_empty() || name.len() > MAX_AUTH_NAME_LEN {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

fn validate_machine_code(machine_code: &str) -> Result<(), AuthorApiError> {
    if machine_code.trim().is_empty() || machine_code.len() > MAX_MACHINE_CODE_LEN {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

fn validate_auth_id(auth_id: &str) -> Result<(), AuthorApiError> {
    if auth_id.trim().is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

fn validate_days(days: i32) -> Result<(), AuthorApiError> {
    if !(1..=MAX_AUTH_DAYS).contains(&days) {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

fn validate_max_streams(max_streams: i32) -> Result<(), AuthorApiError> {
    if !(1..=MAX_AUTH_STREAMS).contains(&max_streams) {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

fn validate_customer_role(role: i32) -> Result<(), AuthorApiError> {
    if !VALID_CUSTOMER_ROLES.contains(&role) {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn valid_create_body() -> Value {
        json!({
            "name": "customer-a",
            "machine_code": "machine-a",
            "days": 30,
            "max_streams": 4,
            "role": 1
        })
    }

    fn valid_update_body() -> Value {
        json!({
            "auth_id": "auth-id",
            "days": 30,
            "max_streams": 4,
            "role": 1
        })
    }

    #[test]
    fn parses_valid_create_authorization_input() {
        let input = parse_create_authorization_input(&valid_create_body()).unwrap();

        assert_eq!(input.name, "customer-a");
        assert_eq!(input.machine_code, "machine-a");
        assert_eq!(input.days, 30);
        assert_eq!(input.max_streams, 4);
        assert_eq!(input.role, 1);
    }

    #[test]
    fn create_input_rejects_missing_or_wrong_typed_fields() {
        let cases = [
            json!({"machine_code":"m","days":30,"max_streams":1,"role":1}),
            json!({"name":"n","days":30,"max_streams":1,"role":1}),
            json!({"name":"n","machine_code":"m","days":"30","max_streams":1,"role":1}),
            json!({"name":"n","machine_code":"m","days":30,"max_streams":"1","role":1}),
            json!({"name":"n","machine_code":"m","days":30,"max_streams":1,"role":"1"}),
        ];

        for body in cases {
            assert!(
                parse_create_authorization_input(&body).is_err(),
                "body should be rejected: {body}"
            );
        }
    }

    #[test]
    fn create_input_rejects_empty_and_too_long_name_or_machine_code() {
        let mut body = valid_create_body();
        body["name"] = json!(" ");
        assert!(parse_create_authorization_input(&body).is_err());

        let mut body = valid_create_body();
        body["name"] = json!("x".repeat(MAX_AUTH_NAME_LEN + 1));
        assert!(parse_create_authorization_input(&body).is_err());

        let mut body = valid_create_body();
        body["machine_code"] = json!(" ");
        assert!(parse_create_authorization_input(&body).is_err());

        let mut body = valid_create_body();
        body["machine_code"] = json!("x".repeat(MAX_MACHINE_CODE_LEN + 1));
        assert!(parse_create_authorization_input(&body).is_err());
    }

    #[test]
    fn create_input_rejects_days_boundaries() {
        for days in [0, -1, MAX_AUTH_DAYS + 1] {
            let mut body = valid_create_body();
            body["days"] = json!(days);
            assert!(
                parse_create_authorization_input(&body).is_err(),
                "days={days} should fail"
            );
        }

        for days in [1, MAX_AUTH_DAYS] {
            let mut body = valid_create_body();
            body["days"] = json!(days);
            assert!(
                parse_create_authorization_input(&body).is_ok(),
                "days={days} should pass"
            );
        }
    }

    #[test]
    fn create_input_rejects_max_streams_boundaries() {
        for max_streams in [0, -1, MAX_AUTH_STREAMS + 1] {
            let mut body = valid_create_body();
            body["max_streams"] = json!(max_streams);
            assert!(
                parse_create_authorization_input(&body).is_err(),
                "max_streams={max_streams} should fail"
            );
        }

        for max_streams in [1, MAX_AUTH_STREAMS] {
            let mut body = valid_create_body();
            body["max_streams"] = json!(max_streams);
            assert!(
                parse_create_authorization_input(&body).is_ok(),
                "max_streams={max_streams} should pass"
            );
        }
    }

    #[test]
    fn create_input_rejects_invalid_customer_roles() {
        for role in [0, -1, 4] {
            let mut body = valid_create_body();
            body["role"] = json!(role);
            assert!(
                parse_create_authorization_input(&body).is_err(),
                "role={role} should fail"
            );
        }

        for role in [1, 2, 3] {
            let mut body = valid_create_body();
            body["role"] = json!(role);
            assert!(
                parse_create_authorization_input(&body).is_ok(),
                "role={role} should pass"
            );
        }
    }

    #[test]
    fn parses_valid_update_authorization_input() {
        let input = parse_update_authorization_input(&valid_update_body()).unwrap();

        assert_eq!(input.auth_id, "auth-id");
        assert_eq!(input.days, 30);
        assert_eq!(input.max_streams, 4);
        assert_eq!(input.role, 1);
    }

    #[test]
    fn update_input_rejects_missing_auth_id_and_invalid_boundaries() {
        let mut body = valid_update_body();
        body["auth_id"] = json!(" ");
        assert!(parse_update_authorization_input(&body).is_err());

        let mut body = valid_update_body();
        body["days"] = json!(0);
        assert!(parse_update_authorization_input(&body).is_err());

        let mut body = valid_update_body();
        body["max_streams"] = json!(0);
        assert!(parse_update_authorization_input(&body).is_err());

        let mut body = valid_update_body();
        body["role"] = json!(4);
        assert!(parse_update_authorization_input(&body).is_err());
    }

    #[test]
    fn rejects_i64_values_outside_i32_range() {
        let mut body = valid_create_body();
        body["days"] = json!(i64::from(i32::MAX) + 1);

        assert!(parse_create_authorization_input(&body).is_err());
    }
}
