use crate::{
    error::ApiError,
    request::{self, Input, Params as Query, Revision},
    StateData,
};
use axum::{
    body::Bytes,
    extract::{DefaultBodyLimit, State},
    http::{header, HeaderMap, HeaderValue},
    response::{IntoResponse, Response},
    routing::get,
    Json, Router,
};
use px_console_store::{AvatarContent, ManagedUser, Username, MAX_AVATAR_BYTES};
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::Arc;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    let avatar = Router::new()
        .route(
            "/api/console/profile/avatar",
            get(read_avatar).put(set_avatar).delete(delete_avatar),
        )
        .layer(DefaultBodyLimit::max(MAX_AVATAR_BYTES));
    Router::new()
        .route("/api/console/profile", get(profile).patch(update_profile))
        .merge(avatar)
}

pub(crate) fn view(profile: ManagedUser) -> Value {
    let avatar_url = profile.has_avatar.then_some("/api/console/profile/avatar");
    json!({
        "id":profile.id,
        "username":profile.username,
        "role":profile.role,
        "authorization_revision":profile.authorization_revision,
        "revision":profile.revision,
        "avatar_url":avatar_url,
        "created_at":profile.created_at
    })
}

async fn profile(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(view(
        state.db.identity().profile(&token, client).await?,
    )))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct ProfileChange {
    revision: i64,
    username: String,
}

async fn update_profile(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<ProfileChange>,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    let username = Username::parse(&input.username)?;
    Ok(Json(view(
        state
            .db
            .identity()
            .update_profile(&token, client, input.revision, &username)
            .await?,
    )))
}

async fn set_avatar(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(input): Query<Revision>,
    body: Bytes,
) -> Result<Json<Value>, ApiError> {
    if headers.get_all(header::CONTENT_TYPE).iter().count() != 1 {
        return Err(ApiError::Invalid);
    }
    let media_type = headers
        .get(header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .ok_or(ApiError::Invalid)?;
    let avatar = AvatarContent::new(media_type, body.to_vec())?;
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(view(
        state
            .db
            .identity()
            .set_avatar(&token, client, input.revision, &avatar)
            .await?,
    )))
}

async fn read_avatar(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Response, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    let avatar = state.db.identity().avatar(&token, client).await?;
    let media_type = HeaderValue::from_str(&avatar.media_type).map_err(|_| ApiError::Internal)?;
    if avatar.sha256.len() != 32 {
        return Err(ApiError::Internal);
    }
    let etag = HeaderValue::from_str(&format!("\"{}\"", hex::encode(&avatar.sha256)))
        .map_err(|_| ApiError::Internal)?;
    let mut response = avatar.image_bytes.into_response();
    response
        .headers_mut()
        .insert(header::CONTENT_TYPE, media_type);
    response.headers_mut().insert(header::ETAG, etag);
    response.headers_mut().insert(
        header::CACHE_CONTROL,
        HeaderValue::from_static("private, no-store"),
    );
    response.headers_mut().insert(
        header::X_CONTENT_TYPE_OPTIONS,
        HeaderValue::from_static("nosniff"),
    );
    Ok(response)
}

async fn delete_avatar(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(input): Query<Revision>,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(view(
        state
            .db
            .identity()
            .delete_avatar(&token, client, input.revision)
            .await?,
    )))
}
