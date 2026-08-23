use crate::author_api_error::AuthorApiError;
use crate::author_api_error::AuthorApiError::AppkeySecretNotPaired;
use crate::author_http_util::{get_body, get_body_int, get_body_str, get_int_param, get_str_param};
use crate::author_keys::{
    KEY_CREATE_AUTHORIZATION_AUTH_ID, KEY_CREATE_AUTHORIZATION_DAYS,
    KEY_CREATE_AUTHORIZATION_MACHINE_CODE, KEY_CREATE_AUTHORIZATION_MAX_DEVICES,
    KEY_CREATE_AUTHORIZATION_MAX_STREAMS, KEY_CREATE_AUTHORIZATION_ROLE,
    KEY_CREATE_AUTHORIZATION_USER_NAME,
};
use crate::author_license_keys::sign_authorization_model;
use crate::authorization_manager::AuthorizationError;
use crate::gAuthorizationManager;
use crate::gLicenseSigner;
use axum::Json;
use axum::body::Body;
use axum::extract::Query;
use px_auth_mgr::app_secret_util::is_appkey_secret_paired;
use px_auth_mgr::auth_license::{LicenseVerifier, SignedLicense};
use px_auth_mgr::authorization::{
    Authorization, AuthorizationVo, MODE_LICENSED, MODE_TRIAL, PRODUCT_GOPICO, TRIAL_DAYS,
    default_trial_max_devices, is_device_product, normalize_product,
};
use px_base::{RespMessage, ok_resp};
use serde::{Deserialize, Serialize};
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
        Err(e) => Err(map_authorization_error(e)),
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

pub async fn handle_verify_license(body: Body) -> Result<Json<RespMessage<bool>>, AuthorApiError> {
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
    let now_ms = px_base::get_current_timestamp();
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
    let current_ts = px_base::get_current_timestamp();
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
    let role = optional_body_role(body)?;

    validate_name(&name)?;
    validate_machine_code(&machine_code)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;

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
    let role = optional_body_role(body)?;

    validate_auth_id(&auth_id)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;

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

/// Customer role is deprecated: accepted (and validated) when present for
/// backward compatibility, but optional and defaults to 1.
fn optional_body_role(body: &Value) -> Result<i32, AuthorApiError> {
    if body.get(KEY_CREATE_AUTHORIZATION_ROLE).is_some() {
        let role = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_ROLE)?;
        validate_customer_role(role)?;
        Ok(role)
    } else {
        Ok(1)
    }
}

fn validate_customer_role(role: i32) -> Result<(), AuthorApiError> {
    if !VALID_CUSTOMER_ROLES.contains(&role) {
        return Err(AuthorApiError::InvalidParams);
    }
    Ok(())
}

fn map_authorization_error(e: AuthorizationError) -> AuthorApiError {
    match e {
        AuthorizationError::AlreadyExist => AuthorApiError::AlreadyExists,
        AuthorizationError::NotFound => AuthorApiError::AuthorizationNotFound,
        AuthorizationError::DatabaseError | AuthorizationError::TimeConvertError => {
            AuthorApiError::DatabaseError
        }
    }
}

// --- GoPico-specific APIs -------------------------------------------------

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct GopicoClientReportResponse {
    pub accepted: bool,
    pub reason: String,
}

/// `POST /api/v1/gopico/report` — licensed clients periodically report their
/// runtime status. No login required; the deploy string itself is the credential
/// (Ed25519 signature + machine binding are verified before recording).
///
/// Body: `{ "data": deploy_str, "machine_code": str, "client_version": str,
///          "client_status": str, "os": str, "device_count": int }`
/// Note: expiry/revocation do NOT block reporting — reports from expired or
/// revoked clients are exactly what an operator wants to see.
pub async fn handle_gopico_client_report(
    body: Body,
) -> Result<Json<RespMessage<GopicoClientReportResponse>>, AuthorApiError> {
    let body = get_body(body).await?;
    let value: Value = serde_json::from_str(&body).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_str = get_body_str(&value, "data")?;
    let machine_code = get_body_str(&value, "machine_code")?;
    let opt_str = |key: &str| {
        value
            .get(key)
            .and_then(|v| v.as_str())
            .unwrap_or_default()
            .to_string()
    };
    let client_version = opt_str("client_version");
    let client_status = opt_str("client_status");
    let client_os = opt_str("os");
    let device_count = value
        .get("device_count")
        .and_then(|v| v.as_i64())
        .unwrap_or(0) as i32;

    let signed =
        SignedLicense::parse_deploy_string(&auth_str).map_err(|_| AuthorApiError::InvalidParams)?;

    let signer_guard = gLicenseSigner.lock().await;
    let signer = signer_guard
        .as_ref()
        .ok_or(AuthorApiError::CantCreateAuthorization)?;
    let verifier = LicenseVerifier::from_public_key_bytes(&signer.public_key_bytes())
        .map_err(|_| AuthorApiError::DatabaseError)?;

    let mut accepted = true;
    let mut reason = String::new();

    if !verifier
        .verify_signature(&signed)
        .map_err(|_| AuthorApiError::DatabaseError)?
    {
        accepted = false;
        reason = "invalid_signature".to_string();
    } else if signed.license.product != PRODUCT_GOPICO {
        accepted = false;
        reason = "wrong_product".to_string();
    } else if signed.license.machine_code != machine_code {
        accepted = false;
        reason = "machine_mismatch".to_string();
    } else {
        let auth_id = signed.license.auth_id.clone();
        match gAuthorizationManager
            .query_authorization_by_id(auth_id.clone())
            .await
        {
            Some(_) => {
                let ok = gAuthorizationManager
                    .update_client_report(
                        &auth_id,
                        &client_version,
                        &client_status,
                        &client_os,
                        device_count,
                        px_base::get_current_timestamp(),
                    )
                    .await;
                if !ok {
                    accepted = false;
                    reason = "update_failed".to_string();
                }
            }
            None => {
                accepted = false;
                reason = "not_found".to_string();
            }
        }
    }

    Ok(Json(ok_resp(GopicoClientReportResponse {
        accepted,
        reason,
    })))
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct GopicoOnlineVerifyResponse {
    pub valid: bool,
    pub reason: String,
    pub expires_at_ms: i64,
    pub max_devices: i32,
    pub revoked: bool,
    pub auth_id: String,
    pub product: String,
}

pub async fn handle_gopico_create_new_deploy_authorization(
    body: Body,
) -> Result<Json<RespMessage<String>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let input = parse_gopico_create_authorization_input(&r)?;

    let auth = gAuthorizationManager
        .gen_new_authorization_for_product(
            input.name,
            input.machine_code,
            input.days,
            input.max_streams,
            input.role,
            PRODUCT_GOPICO.to_string(),
        )
        .await
        .map_err(|e| match e {
            AuthorizationError::AlreadyExist => AuthorApiError::AlreadyExists,
            _ => AuthorApiError::DatabaseError,
        })?;

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

pub async fn handle_gopico_update_authorization(
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let input = parse_gopico_update_authorization_input(&r)?;

    let auth = gAuthorizationManager
        .query_authorization_by_id(input.auth_id.clone())
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    if auth.product != PRODUCT_GOPICO {
        return Err(AuthorApiError::InvalidParams);
    }

    let mut auth = auth;
    let current_ts = px_base::get_current_timestamp();
    auth.days = input.days;
    auth.role = input.role;
    auth.max_streams = input.max_streams;
    auth.created_timestamp_ms = current_ts;
    auth.end_timestamp_ms = current_ts + (input.days as i64) * 24 * 3600 * 1000;
    auth.last_modify_timestamp = current_ts;
    // Renewing clears soft-revoke so a fresh deploy can be issued.
    auth.revoked = false;
    auth.revoked_at_ms = 0;

    if !gAuthorizationManager
        .update_authorization(input.auth_id.clone(), auth)
        .await
    {
        return Err(AuthorApiError::UpdateAuthFailed);
    }
    let auth = gAuthorizationManager
        .query_authorization_by_id(input.auth_id)
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    Ok(Json(ok_resp(auth)))
}

pub async fn handle_gopico_query_authorizations(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<AuthorizationVo>>>, AuthorApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let auth = gAuthorizationManager
        .query_authorizations_by_product(PRODUCT_GOPICO.to_string(), page, page_size)
        .await
        .map_err(|_| AuthorApiError::AuthorizationNotFound)?;
    Ok(Json(ok_resp(auth)))
}

pub async fn handle_gopico_query_deploy_authorization_by_id(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<String>>, AuthorApiError> {
    let auth_id = get_str_param(&query, "auth_id")?;
    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id)
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    if auth.product != PRODUCT_GOPICO {
        return Err(AuthorApiError::InvalidParams);
    }
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

pub async fn handle_gopico_revoke_authorization(
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_id = get_body_str(&r, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    validate_auth_id(&auth_id)?;

    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id.clone())
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    if auth.product != PRODUCT_GOPICO {
        return Err(AuthorApiError::InvalidParams);
    }

    let auth = gAuthorizationManager
        .revoke_authorization(auth_id)
        .await
        .map_err(|e| match e {
            AuthorizationError::NotFound => AuthorApiError::AuthorizationNotFound,
            _ => AuthorApiError::UpdateAuthFailed,
        })?;
    Ok(Json(ok_resp(auth)))
}

pub async fn handle_gopico_verify_online(
    body: Body,
) -> Result<Json<RespMessage<GopicoOnlineVerifyResponse>>, AuthorApiError> {
    let body = get_body(body).await?;
    let value: Value = serde_json::from_str(&body).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_str = get_body_str(&value, "data")?;
    let machine_code = get_body_str(&value, "machine_code")?;

    let signed =
        SignedLicense::parse_deploy_string(&auth_str).map_err(|_| AuthorApiError::InvalidParams)?;

    let signer_guard = gLicenseSigner.lock().await;
    let signer = signer_guard
        .as_ref()
        .ok_or(AuthorApiError::CantCreateAuthorization)?;
    let verifier = LicenseVerifier::from_public_key_bytes(&signer.public_key_bytes())
        .map_err(|_| AuthorApiError::DatabaseError)?;

    let now_ms = px_base::get_current_timestamp();
    let mut reason = String::new();
    let mut valid = true;

    if !verifier
        .verify_signature(&signed)
        .map_err(|_| AuthorApiError::DatabaseError)?
    {
        valid = false;
        reason = "invalid_signature".to_string();
    } else if signed.license.product != PRODUCT_GOPICO {
        valid = false;
        reason = "wrong_product".to_string();
    } else if signed.license.machine_code != machine_code {
        valid = false;
        reason = "machine_mismatch".to_string();
    } else if signed.license.expires_at_ms <= now_ms {
        valid = false;
        reason = "expired".to_string();
    }

    let mut revoked = false;
    if valid {
        match gAuthorizationManager
            .query_authorization_by_id(signed.license.auth_id.clone())
            .await
        {
            Some(auth) => {
                if auth.revoked {
                    valid = false;
                    revoked = true;
                    reason = "revoked".to_string();
                } else if auth.product != PRODUCT_GOPICO {
                    valid = false;
                    reason = "wrong_product".to_string();
                }
            }
            None => {
                valid = false;
                reason = "not_found".to_string();
            }
        }
    }

    Ok(Json(ok_resp(GopicoOnlineVerifyResponse {
        valid,
        reason,
        expires_at_ms: signed.license.expires_at_ms,
        max_devices: signed.license.max_streams,
        revoked,
        auth_id: signed.license.auth_id,
        product: signed.license.product,
    })))
}

fn parse_gopico_create_authorization_input(
    body: &Value,
) -> Result<CreateAuthorizationInput, AuthorApiError> {
    let name = get_body_str(body, KEY_CREATE_AUTHORIZATION_USER_NAME)?;
    let machine_code = get_body_str(body, KEY_CREATE_AUTHORIZATION_MACHINE_CODE)?;
    let days = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_DAYS)?;
    // Prefer max_devices; fall back to max_streams for convenience.
    let max_streams = if body.get(KEY_CREATE_AUTHORIZATION_MAX_DEVICES).is_some() {
        checked_body_i32(body, KEY_CREATE_AUTHORIZATION_MAX_DEVICES)?
    } else {
        checked_body_i32(body, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)?
    };
    let role = optional_body_role(body)?;

    validate_name(&name)?;
    validate_machine_code(&machine_code)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;

    Ok(CreateAuthorizationInput {
        name,
        machine_code,
        days,
        max_streams,
        role,
    })
}

fn parse_gopico_update_authorization_input(
    body: &Value,
) -> Result<UpdateAuthorizationInput, AuthorApiError> {
    let auth_id = get_body_str(body, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    let days = checked_body_i32(body, KEY_CREATE_AUTHORIZATION_DAYS)?;
    let max_streams = if body.get(KEY_CREATE_AUTHORIZATION_MAX_DEVICES).is_some() {
        checked_body_i32(body, KEY_CREATE_AUTHORIZATION_MAX_DEVICES)?
    } else {
        checked_body_i32(body, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)?
    };
    let role = optional_body_role(body)?;

    validate_auth_id(&auth_id)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;

    Ok(UpdateAuthorizationInput {
        auth_id,
        days,
        max_streams,
        role,
    })
}

// --- Device self-registration & license pull --------------------------------

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct DevicePullResponse {
    pub deploy_str: String,
    pub auth_id: String,
    pub mode: String,
    pub days: i32,
    pub max_devices: i32,
    pub role: i32,
    pub expires_at_ms: i64,
    pub revoked: bool,
    pub registered_new: bool,
    pub server_time_ms: i64,
    pub last_modify_timestamp: i64,
    /// 服务器口径的已使用时间：days - 剩余，夹在 [0, days]。客户端直接展示，
    /// 不做本地计时/时钟纠正（网络失败时沿用上一次拉到的值）。
    #[serde(default)]
    pub used_time_ms: i64,
}

/// `POST /api/v1/device/pull` — devices (gopico / clientbox / goagent) call this
/// periodically (and on demand) to fetch their latest license. Unknown devices
/// are auto-registered in trial mode. No login required: the device code IS the
/// identity; the signed license it receives only works on that device.
///
/// Body: `{ "product": "gopico|clientbox|goagent", "device_code": str,
///          "client_version": str?, "client_status": str?, "os": str?,
///          "device_count": int? }`
pub async fn handle_device_pull(
    body: Body,
) -> Result<Json<RespMessage<DevicePullResponse>>, AuthorApiError> {
    let body = get_body(body).await?;
    let value: Value = serde_json::from_str(&body).map_err(|_| AuthorApiError::InvalidParams)?;
    let product = normalize_product(&get_body_str(&value, "product")?).to_string();
    let device_code = get_body_str(&value, "device_code")?;
    validate_machine_code(&device_code)?;
    if !is_device_product(&product) {
        return Err(AuthorApiError::InvalidParams);
    }
    let opt_str = |key: &str| {
        value
            .get(key)
            .and_then(|v| v.as_str())
            .unwrap_or_default()
            .to_string()
    };
    let client_version = opt_str("client_version");
    let client_status = opt_str("client_status");
    let client_os = opt_str("os");
    let device_count = value
        .get("device_count")
        .and_then(|v| v.as_i64())
        .unwrap_or(0) as i32;

    let now_ms = px_base::get_current_timestamp();
    let mut registered_new = false;

    let auth = match gAuthorizationManager
        .query_authorization_by_machine_code_product(&device_code, &product)
        .await
    {
        Some(auth) => auth,
        None => {
            // Auto-register as a trial device.
            let name = format!("{product}:{device_code}");
            let created = gAuthorizationManager
                .gen_new_authorization_for_product(
                    name,
                    device_code.clone(),
                    TRIAL_DAYS,
                    default_trial_max_devices(&product),
                    1,
                    product.clone(),
                )
                .await;
            match created {
                Ok(mut auth) => {
                    auth.mode = MODE_TRIAL.to_string();
                    if !gAuthorizationManager
                        .update_authorization(auth.auth_id.clone(), auth.clone())
                        .await
                    {
                        return Err(AuthorApiError::DatabaseError);
                    }
                    registered_new = true;
                    auth
                }
                Err(AuthorizationError::AlreadyExist) => {
                    // Lost a concurrent-registration race; re-query.
                    gAuthorizationManager
                        .query_authorization_by_machine_code_product(&device_code, &product)
                        .await
                        .ok_or(AuthorApiError::DatabaseError)?
                }
                Err(_) => return Err(AuthorApiError::DatabaseError),
            }
        }
    };

    // Telemetry: best-effort, never blocks the pull.
    let _ = gAuthorizationManager
        .update_client_report(
            &auth.auth_id,
            &client_version,
            &client_status,
            &client_os,
            device_count,
            now_ms,
        )
        .await;

    let signer_guard = gLicenseSigner.lock().await;
    let signer = signer_guard
        .as_ref()
        .ok_or(AuthorApiError::CantCreateAuthorization)?;
    let signed = sign_authorization_model(signer, &auth)
        .map_err(|_| AuthorApiError::CantCreateAuthorization)?;
    let deploy_str = signed
        .to_deploy_string()
        .map_err(|_| AuthorApiError::CantCreateAuthorization)?;

    // 服务器口径已使用时间：days - 剩余（锚定在服务器时钟上）。
    let total_ms = (auth.days as i64) * 24 * 60 * 60 * 1000;
    let used_time_ms = (total_ms - (auth.end_timestamp_ms - now_ms)).clamp(0, total_ms);

    Ok(Json(ok_resp(DevicePullResponse {
        deploy_str,
        auth_id: auth.auth_id,
        mode: auth.mode,
        days: auth.days,
        max_devices: auth.max_streams,
        role: auth.role,
        expires_at_ms: auth.end_timestamp_ms,
        revoked: auth.revoked,
        registered_new,
        server_time_ms: now_ms,
        last_modify_timestamp: auth.last_modify_timestamp,
        used_time_ms,
    })))
}

/// `GET /api/v1/product/query/authorizations?product=&page=&page_size=` —
/// admin lists device authorizations of one product category.
pub async fn handle_product_query_authorizations(
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<AuthorizationVo>>>, AuthorApiError> {
    let product = normalize_product(&get_str_param(&query, "product")?).to_string();
    if !is_device_product(&product) {
        return Err(AuthorApiError::InvalidParams);
    }
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let auth = gAuthorizationManager
        .query_authorizations_by_product(product, page, page_size)
        .await
        .map_err(|_| AuthorApiError::AuthorizationNotFound)?;
    Ok(Json(ok_resp(auth)))
}

/// `POST /api/v1/device/update/authorization` — admin edits a device
/// authorization. Body: `{ auth_id, mode: "trial"|"licensed", days,
/// max_devices (or max_streams), role }`.
/// - `licensed`: validity window is re-anchored to now + days.
/// - `trial`: days is forced to TRIAL_DAYS (pseudo-permanent).
/// Switching mode clears soft-revoke so the next pull delivers a usable license.
pub async fn handle_device_update_authorization(
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_id = get_body_str(&r, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    validate_auth_id(&auth_id)?;
    let mode = get_body_str(&r, "mode")?;
    if mode != MODE_TRIAL && mode != MODE_LICENSED {
        return Err(AuthorApiError::InvalidParams);
    }
    let mut days = checked_body_i32(&r, KEY_CREATE_AUTHORIZATION_DAYS)?;
    let max_streams = if r.get(KEY_CREATE_AUTHORIZATION_MAX_DEVICES).is_some() {
        checked_body_i32(&r, KEY_CREATE_AUTHORIZATION_MAX_DEVICES)?
    } else {
        checked_body_i32(&r, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)?
    };
    let role = optional_body_role(&r)?;
    validate_days(days)?;
    validate_max_streams(max_streams)?;

    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id.clone())
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    if !is_device_product(&auth.product) {
        return Err(AuthorApiError::InvalidParams);
    }

    if mode == MODE_TRIAL {
        days = TRIAL_DAYS;
    }

    let mut auth = auth;
    let current_ts = px_base::get_current_timestamp();
    auth.mode = mode;
    auth.days = days;
    auth.role = role;
    auth.max_streams = max_streams;
    auth.created_timestamp_ms = current_ts;
    auth.end_timestamp_ms = current_ts + (days as i64) * 24 * 3600 * 1000;
    auth.last_modify_timestamp = current_ts;
    auth.revoked = false;
    auth.revoked_at_ms = 0;

    if !gAuthorizationManager
        .update_authorization(auth_id.clone(), auth)
        .await
    {
        return Err(AuthorApiError::UpdateAuthFailed);
    }
    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id)
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    Ok(Json(ok_resp(auth)))
}

/// `POST /api/v1/device/revoke/authorization` — admin soft-revokes a device
/// authorization of any device product. Body: `{ auth_id }`.
pub async fn handle_device_revoke_authorization(
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_id = get_body_str(&r, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    validate_auth_id(&auth_id)?;

    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id.clone())
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    if !is_device_product(&auth.product) {
        return Err(AuthorApiError::InvalidParams);
    }

    let auth = gAuthorizationManager
        .revoke_authorization(auth_id)
        .await
        .map_err(|e| match e {
            AuthorizationError::NotFound => AuthorApiError::AuthorizationNotFound,
            _ => AuthorApiError::UpdateAuthFailed,
        })?;
    Ok(Json(ok_resp(auth)))
}

/// `POST /api/v1/device/delete/authorization` — admin hard-deletes a device
/// authorization of any device product from the database. Body: `{ auth_id }`.
/// 不可恢复，返回 data=true 表示删除成功。
pub async fn handle_device_delete_authorization(
    body: Body,
) -> Result<Json<RespMessage<bool>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let auth_id = get_body_str(&r, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    validate_auth_id(&auth_id)?;

    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id.clone())
        .await
        .ok_or(AuthorApiError::AuthorizationNotFound)?;
    if !is_device_product(&auth.product) {
        return Err(AuthorApiError::InvalidParams);
    }

    gAuthorizationManager
        .delete_authorization(auth_id)
        .await
        .map_err(|e| match e {
            AuthorizationError::NotFound => AuthorApiError::AuthorizationNotFound,
            _ => AuthorApiError::UpdateAuthFailed,
        })?;
    Ok(Json(ok_resp(true)))
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

    #[test]
    fn gopico_create_accepts_max_devices() {
        let body = json!({
            "name": "gopico-a",
            "machine_code": "mc-gopico",
            "days": 30,
            "max_devices": 40,
            "role": 1
        });
        let input = parse_gopico_create_authorization_input(&body).unwrap();
        assert_eq!(input.max_streams, 40);
        assert_eq!(input.name, "gopico-a");
    }
}
