use crate::{
    error::ApiError,
    request::{self, Input},
    StateData,
};
use axum::{
    extract::{ConnectInfo, State},
    http::{HeaderMap, StatusCode},
    Json,
};
use px_console_store::{ClientType, PasswordDigest, Role, StoreError, Username};
use serde::Deserialize;
use serde_json::{json, Value};
use std::{net::SocketAddr, sync::Arc};
use zeroize::Zeroizing;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Credentials {
    username: String,
    password: String,
}
pub async fn register(
    State(state): State<Arc<StateData>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Input(input): Input<Credentials>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    let password = Zeroizing::new(input.password);
    let client = request::client(&headers)?;
    state.policy.check(&headers, client)?;
    if !state.policy.registration || client == ClientType::AdminWeb {
        return Err(ApiError::Rejected);
    }
    let name = Username::parse(&input.username)?;
    if !state.limits.allow(name.normalized(), peer.ip()) {
        return Err(ApiError::RateLimited);
    }
    let password = hash(&state, password).await?;
    state.active()?;
    let profile = state.db.identity().register(&name, &password).await?;
    Ok((StatusCode::CREATED, Json(json!(profile))))
}
pub async fn login(
    State(state): State<Arc<StateData>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Input(input): Input<Credentials>,
) -> Result<Json<Value>, ApiError> {
    let password = Zeroizing::new(input.password);
    let client = request::client(&headers)?;
    state.policy.check(&headers, client)?;
    let name = Username::parse(&input.username).map_err(|_| ApiError::Unauthorized)?;
    if !px_credentials::valid_password(&password) {
        return Err(ApiError::Unauthorized);
    }
    if !state.limits.allow(name.normalized(), peer.ip()) {
        return Err(ApiError::RateLimited);
    }
    let permit = state
        .slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::RateLimited)?;
    let identity = state.db.identity();
    let credential = match identity.credential(&name).await {
        Ok(row) => Some(row),
        Err(StoreError::Rejected) => None,
        Err(store_error) => return Err(store_error.into()),
    };
    let encoded = credential.as_ref().map_or_else(
        || state.dummy.clone(),
        |stored_credential| Zeroizing::new(stored_credential.password.encoded().into()),
    );
    let valid = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        px_credentials::verify(&password, &encoded)
    })
    .await
    .map_err(|_| ApiError::Internal)?;
    let credential = credential.filter(|_| valid).ok_or(ApiError::Unauthorized)?;
    if client == ClientType::AdminWeb && credential.role == Role::User {
        return Err(ApiError::Unauthorized);
    }
    state.active()?;
    let (token, digest) = request::mint();
    let session = identity
        .issue_session(
            credential.user.id,
            credential.user.authorization_revision,
            &digest,
            client,
            state.policy.session_lifetime,
        )
        .await?;
    let profile = crate::profile_api::view(identity.profile(&digest, client).await?);
    Ok(Json(
        json!({"token":token.as_str(),"expires_at":session.expires_at,"client_type":client,"profile":profile}),
    ))
}
pub async fn profile(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(crate::profile_api::view(
        state.db.identity().profile(&token, client).await?,
    )))
}
pub async fn logout(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    let identity = state.db.identity();
    let session = identity.authenticate(&token, client).await?;
    identity
        .revoke_session(session.user_id, session.session_id)
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PasswordChange {
    current_password: String,
    new_password: String,
}
pub async fn change_password(
    State(state): State<Arc<StateData>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Input(input): Input<PasswordChange>,
) -> Result<StatusCode, ApiError> {
    let password = Zeroizing::new(input.current_password);
    let new_password = Zeroizing::new(input.new_password);
    let (token, client) = request::context(&state, &headers)?;
    let identity = state.db.identity();
    let credential = identity.session_credential(&token, client).await?;
    if !state
        .limits
        .allow(&credential.user.id.to_string(), peer.ip())
    {
        return Err(ApiError::RateLimited);
    }
    if !px_credentials::valid_password(&password) || !px_credentials::valid_password(&new_password)
    {
        return Err(ApiError::Invalid);
    }
    let permit = state
        .slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::RateLimited)?;
    let revision = credential.user.authorization_revision;
    let password = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        if !px_credentials::verify(&password, credential.password.encoded()) {
            return Err(ApiError::Unauthorized);
        }
        px_credentials::hash(&new_password).map_err(|_| ApiError::Internal)
    })
    .await
    .map_err(|_| ApiError::Internal)??;
    state.active()?;
    identity
        .change_password(
            &token,
            client,
            revision,
            &PasswordDigest::parse(password.to_string())?,
        )
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
pub(crate) async fn hash(
    state: &StateData,
    password: Zeroizing<String>,
) -> Result<PasswordDigest, ApiError> {
    if !px_credentials::valid_password(&password) {
        return Err(ApiError::Invalid);
    }
    let permit = state
        .slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::RateLimited)?;
    let encoded = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        px_credentials::hash(&password)
    })
    .await
    .map_err(|_| ApiError::Internal)?
    .map_err(|_| ApiError::Internal)?;
    Ok(PasswordDigest::parse(encoded.to_string())?)
}
