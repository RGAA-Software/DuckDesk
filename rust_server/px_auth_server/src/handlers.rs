use crate::{error::ApiError, AppState};
use axum::{
    extract::{ConnectInfo, Path, Query, State},
    http::{header, HeaderMap, StatusCode},
    Json,
};
use px_auth_store::IssueRequest;
use px_credentials as credentials;
use px_license::{Distribution, Product, VerifyContext};
use rand::RngCore;
use serde::Deserialize;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{net::SocketAddr, sync::Arc};
use uuid::Uuid;
use zeroize::Zeroizing;

fn bearer(headers: &HeaderMap) -> Result<[u8; 32], ApiError> {
    // Exactly one header. Cookies, query tokens and old custom headers are not identities.
    if headers.get_all(header::AUTHORIZATION).iter().count() != 1 {
        return Err(ApiError::Unauthorized);
    }
    let value = headers
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .ok_or(ApiError::Unauthorized)?;
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err(ApiError::Unauthorized);
    }
    Ok(Sha256::digest(value.as_bytes()).into())
}
pub async fn ready(State(state): State<Arc<AppState>>) -> Result<StatusCode, ApiError> {
    state.store.ready(state.deployment).await?;
    Ok(StatusCode::NO_CONTENT)
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Login {
    username: String,
    password: String,
}
pub async fn login(
    State(state): State<Arc<AppState>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(input): Json<Login>,
) -> Result<Json<Value>, ApiError> {
    let username =
        credentials::normalize_username(&input.username).ok_or(ApiError::Unauthorized)?;
    let password = Zeroizing::new(input.password);
    if !credentials::valid_password(&password) {
        return Err(ApiError::Unauthorized);
    }
    let client_address = state
        .client_address_resolver
        .resolve(peer.ip(), &headers)
        .map_err(|_| ApiError::Invalid)?;
    if !state.limits.allow(&username, client_address) {
        return Err(ApiError::RateLimited);
    }
    let permit = state
        .login_slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::RateLimited)?;
    let credential = state.operators.credential(&username).await?;
    let encoded = credential.as_ref().map_or_else(
        || state.dummy_password.clone(),
        |value| value.password_hash.clone(),
    );
    // The permit lives inside blocking work even if the requesting connection disappears.
    let verified = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        credentials::verify(&password, &encoded)
    })
    .await
    .map_err(|_| ApiError::Internal)?;
    let credential = credential
        .filter(|_| verified)
        .ok_or(ApiError::Unauthorized)?;
    let mut entropy = [0; 32];
    rand::rng().fill_bytes(&mut entropy);
    let token = Zeroizing::new(hex::encode(entropy));
    let digest: [u8; 32] = Sha256::digest(token.as_bytes()).into();
    let session = state
        .operators
        .issue_session(credential.id, credential.authorization_revision, &digest)
        .await?;
    let profile = state.operators.authenticate(&digest).await?;
    Ok(Json(
        json!({"token":token.as_str(),"expires_at":session.expires_at,"profile":profile}),
    ))
}
pub async fn logout(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    state.operators.revoke_session(&bearer(&headers)?).await?;
    Ok(StatusCode::NO_CONTENT)
}
pub async fn me(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state.operators.authenticate(&bearer(&headers)?).await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Page {
    after: Option<Uuid>,
    limit: u32,
}
pub async fn customers(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .store
            .list_customers(&bearer(&headers)?, page.after, page.limit)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewCustomer {
    name: String,
    remark: String,
}
pub async fn create_customer(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Json(input): Json<NewCustomer>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    Ok((
        StatusCode::CREATED,
        Json(json!(
            state
                .store
                .create_customer(&bearer(&headers)?, &input.name, &input.remark)
                .await?
        )),
    ))
}
pub async fn licenses(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .store
            .list_licenses(&bearer(&headers)?, page.after, page.limit)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Issue {
    request_id: Uuid,
    request: IssueRequest,
}
pub async fn issue(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Json(input): Json<Issue>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .store
            .issue(&bearer(&headers)?, input.request_id, input.request)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Revision {
    expected_revision: i64,
}
pub async fn revoke(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Json(input): Json<Revision>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(
        json!({"revision":state.store.revoke(&bearer(&headers)?,id,input.expected_revision).await?}),
    ))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Verify {
    wire: String,
    deployment_id: Uuid,
    product: Product,
    distribution: Distribution,
    release_namespace: String,
    oem_id: Option<String>,
    machine_sha256: String,
}
pub async fn verify(
    State(state): State<Arc<AppState>>,
    Json(input): Json<Verify>,
) -> Result<Json<Value>, ApiError> {
    let now = state.store.database_time().await?;
    let payload = state
        .verifier
        .verify(
            &input.wire,
            &VerifyContext {
                deployment_id: input.deployment_id,
                product: input.product,
                distribution: input.distribution,
                release_namespace: &input.release_namespace,
                oem_id: input.oem_id.as_deref(),
                machine_sha256: &input.machine_sha256,
                now,
                minimum_revision: 1,
                last_trusted_time: 0,
            },
        )
        .map_err(|_| ApiError::Unauthorized)?;
    // The signed wire and all explicit target bindings authenticate this exact consumer
    // contact. Internal outbox lease IDs remain private; revoked or superseded consumers
    // still confirm contact before currentness is rejected, then fail closed locally.
    state
        .store
        .acknowledge_consumer_contact(payload.license_id)
        .await?;
    let current = state
        .store
        .current(payload.license_id, payload.revision)
        .await?;
    if current.wire != input.wire {
        return Err(ApiError::Unauthorized);
    }
    Ok(Json(
        json!({"license_id":payload.license_id,"revision":payload.revision,"verified_at":now}),
    ))
}
pub async fn authors(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .operators
            .list(&bearer(&headers)?, page.after, page.limit)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewAuthor {
    username: String,
    password: String,
    role: String,
}
async fn digest_password(
    state: &Arc<AppState>,
    token: &[u8; 32],
    password: String,
) -> Result<Zeroizing<String>, ApiError> {
    let password = Zeroizing::new(password);
    if state.operators.authenticate(token).await?.role != "admin" {
        return Err(ApiError::Unauthorized);
    }
    if !credentials::valid_password(&password) {
        return Err(ApiError::Invalid);
    }
    let permit = state
        .login_slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::RateLimited)?;
    tokio::task::spawn_blocking(move || {
        let _permit = permit;
        credentials::hash(&password)
    })
    .await
    .map_err(|_| ApiError::Internal)?
    .map_err(|_| ApiError::Invalid)
}
pub async fn create_author(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Json(input): Json<NewAuthor>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    let username = credentials::normalize_username(&input.username).ok_or(ApiError::Invalid)?;
    let token = bearer(&headers)?;
    let digest = digest_password(&state, &token, input.password).await?;
    Ok((
        StatusCode::CREATED,
        Json(json!(
            state
                .operators
                .create(&token, &username, &digest, &input.role)
                .await?
        )),
    ))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PasswordChange {
    expected_revision: i64,
    password: String,
}
pub async fn set_password(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Json(input): Json<PasswordChange>,
) -> Result<StatusCode, ApiError> {
    let token = bearer(&headers)?;
    let digest = digest_password(&state, &token, input.password).await?;
    state
        .operators
        .set_password(&token, id, input.expected_revision, &digest)
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
