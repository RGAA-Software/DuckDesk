use crate::{
    error::ApiError,
    identity,
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    Json,
};
use px_console_store::{Role, Username};
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::Arc;
use uuid::Uuid;
use zeroize::Zeroizing;
pub async fn users(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    let token = request::administrator(&state, &headers)?;
    Ok(Json(json!(
        state
            .db
            .control()
            .list_users(&token, page.after, page.limit)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewUser {
    username: String,
    password: String,
    role: Role,
}
pub async fn create_user(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<NewUser>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    let password = Zeroizing::new(input.password);
    let token = request::administrator(&state, &headers)?;
    // Verify the management identity before allocating expensive password work.
    let profile = state
        .db
        .identity()
        .profile(&token, px_console_store::ClientType::AdminWeb)
        .await?;
    if profile.role != "admin" {
        return Err(ApiError::Rejected);
    }
    let name = Username::parse(&input.username)?;
    let password = identity::hash(&state, password).await?;
    state.active()?;
    Ok((
        StatusCode::CREATED,
        Json(json!(
            state
                .db
                .control()
                .create_user(&token, &name, &password, input.role)
                .await?
        )),
    ))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UserChange {
    revision: i64,
    role: Role,
    disabled: bool,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PasswordReset {
    revision: i64,
    password: String,
}
pub async fn reset_password(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<PasswordReset>,
) -> Result<Json<Value>, ApiError> {
    let password = Zeroizing::new(input.password);
    let token = request::administrator(&state, &headers)?;
    let profile = state
        .db
        .identity()
        .profile(&token, px_console_store::ClientType::AdminWeb)
        .await?;
    if profile.role != "admin" {
        return Err(ApiError::Rejected);
    }
    let password = identity::hash(&state, password).await?;
    state.active()?;
    Ok(Json(json!(
        state
            .db
            .control()
            .reset_password(&token, id, input.revision, &password)
            .await?
    )))
}
pub async fn update_user(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<UserChange>,
) -> Result<Json<Value>, ApiError> {
    let token = request::administrator(&state, &headers)?;
    Ok(Json(json!(
        state
            .db
            .control()
            .update_user(&token, id, input.revision, input.role, input.disabled)
            .await?
    )))
}
pub async fn delete_user(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Query(input): Query<Revision>,
) -> Result<StatusCode, ApiError> {
    state
        .db
        .control()
        .delete_user(
            &request::administrator(&state, &headers)?,
            id,
            input.revision,
        )
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
pub async fn groups(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .groups()
            .list(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit
            )
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewGroup {
    name: String,
    remark: String,
}
pub async fn create_group(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<NewGroup>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    Ok((
        StatusCode::CREATED,
        Json(json!(
            state
                .db
                .groups()
                .create(
                    &request::administrator(&state, &headers)?,
                    &input.name,
                    &input.remark
                )
                .await?
        )),
    ))
}
pub async fn group(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .groups()
            .get(&request::administrator(&state, &headers)?, id)
            .await?
    )))
}
pub async fn delete_group(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Query(input): Query<Revision>,
) -> Result<StatusCode, ApiError> {
    state
        .db
        .groups()
        .delete(
            &request::administrator(&state, &headers)?,
            id,
            input.revision,
        )
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
pub async fn members(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .groups()
            .members(&request::administrator(&state, &headers)?, id)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct MemberChange {
    revision: i64,
    members: Vec<Uuid>,
}
pub async fn replace_members(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<MemberChange>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .groups()
            .replace_members(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                &input.members
            )
            .await?
    )))
}
