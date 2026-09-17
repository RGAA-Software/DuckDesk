use crate::{error::ApiError, StateData};
use axum::{
    extract::{rejection::JsonRejection, FromRequest, Json, Request},
    http::{header, HeaderMap},
};
use px_console_store::{ClientType, ResourceCredential, TokenDigest};
use serde::de::DeserializeOwned;
use sha2::{Digest, Sha256};

pub struct Input<T>(pub T);
#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Page {
    pub after: Option<uuid::Uuid>,
    pub limit: u32,
}
#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Revision {
    pub revision: i64,
}
pub struct Params<T>(pub T);
pub struct Route<T>(pub T);
impl<S, T> axum::extract::FromRequestParts<S> for Params<T>
where
    S: Send + Sync,
    T: DeserializeOwned,
{
    type Rejection = ApiError;
    async fn from_request_parts(
        parts: &mut axum::http::request::Parts,
        state: &S,
    ) -> Result<Self, Self::Rejection> {
        let axum::extract::Query(value) =
            axum::extract::Query::<T>::from_request_parts(parts, state)
                .await
                .map_err(|_| ApiError::Invalid)?;
        Ok(Self(value))
    }
}
impl<S, T> axum::extract::FromRequestParts<S> for Route<T>
where
    S: Send + Sync,
    T: DeserializeOwned + Send,
{
    type Rejection = ApiError;
    async fn from_request_parts(
        parts: &mut axum::http::request::Parts,
        state: &S,
    ) -> Result<Self, Self::Rejection> {
        let axum::extract::Path(value) = axum::extract::Path::<T>::from_request_parts(parts, state)
            .await
            .map_err(|_| ApiError::Invalid)?;
        Ok(Self(value))
    }
}
pub fn mint() -> (zeroize::Zeroizing<String>, TokenDigest) {
    use rand::RngCore;
    let mut entropy = zeroize::Zeroizing::new([0_u8; 32]);
    rand::rng().fill_bytes(entropy.as_mut());
    let token = zeroize::Zeroizing::new(hex::encode(*entropy));
    let digest = TokenDigest::from_sha256(Sha256::digest(token.as_bytes()).into());
    (token, digest)
}
impl<S, T> FromRequest<S> for Input<T>
where
    S: Send + Sync,
    T: DeserializeOwned,
{
    type Rejection = ApiError;
    async fn from_request(request: Request, state: &S) -> Result<Self, Self::Rejection> {
        let Json(value) = Json::<T>::from_request(request, state)
            .await
            .map_err(|_: JsonRejection| ApiError::Invalid)?;
        Ok(Self(value))
    }
}
pub fn client(headers: &HeaderMap) -> Result<ClientType, ApiError> {
    if headers.get_all("x-pixels-client-type").iter().count() != 1 {
        return Err(ApiError::Invalid);
    }
    match headers
        .get("x-pixels-client-type")
        .and_then(|value| value.to_str().ok())
    {
        Some("panel") => Ok(ClientType::Panel),
        Some("android") => Ok(ClientType::Android),
        Some("admin_web") => Ok(ClientType::AdminWeb),
        Some("user_web") => Ok(ClientType::UserWeb),
        _ => Err(ApiError::Invalid),
    }
}
pub fn bearer(headers: &HeaderMap) -> Result<TokenDigest, ApiError> {
    if headers.get_all(header::AUTHORIZATION).iter().count() != 1 {
        return Err(ApiError::Unauthorized);
    }
    let value = headers
        .get(header::AUTHORIZATION)
        .and_then(|header_value| header_value.to_str().ok())
        .and_then(|header_value| header_value.strip_prefix("Bearer "))
        .ok_or(ApiError::Unauthorized)?;
    secret_digest(value).ok_or(ApiError::Unauthorized)
}

pub fn secret_digest(value: &str) -> Option<TokenDigest> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
    {
        return None;
    }
    Some(TokenDigest::from_sha256(
        Sha256::digest(value.as_bytes()).into(),
    ))
}
pub fn context(
    state: &StateData,
    headers: &HeaderMap,
) -> Result<(TokenDigest, ClientType), ApiError> {
    let client = client(headers)?;
    state.policy.check(headers, client)?;
    Ok((bearer(headers)?, client))
}
pub fn administrator(state: &StateData, headers: &HeaderMap) -> Result<TokenDigest, ApiError> {
    let (token, client) = context(state, headers)?;
    if client != ClientType::AdminWeb {
        return Err(ApiError::Rejected);
    }
    Ok(token)
}

#[derive(Clone, Copy)]
enum ResourceSubjectKind {
    User,
    Guest,
}

pub struct ResourceContext {
    token: TokenDigest,
    pub client: ClientType,
    subject: ResourceSubjectKind,
}

impl ResourceContext {
    pub fn credential(&self) -> ResourceCredential<'_> {
        match self.subject {
            ResourceSubjectKind::User => ResourceCredential::User(&self.token),
            ResourceSubjectKind::Guest => ResourceCredential::Guest(&self.token),
        }
    }
}

pub fn resource_context(
    state: &StateData,
    headers: &HeaderMap,
) -> Result<ResourceContext, ApiError> {
    const SUBJECT: &str = "x-pixels-subject-kind";
    if headers.get_all(SUBJECT).iter().count() != 1 {
        return Err(ApiError::Invalid);
    }
    let subject = match headers.get(SUBJECT).and_then(|value| value.to_str().ok()) {
        Some("user") => ResourceSubjectKind::User,
        Some("guest") => ResourceSubjectKind::Guest,
        _ => return Err(ApiError::Invalid),
    };
    let (token, client) = context(state, headers)?;
    if client == ClientType::AdminWeb {
        return Err(ApiError::Rejected);
    }
    Ok(ResourceContext {
        token,
        client,
        subject,
    })
}
