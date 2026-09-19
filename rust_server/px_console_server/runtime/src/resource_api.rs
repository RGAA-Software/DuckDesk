use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::{get, post},
    Json, Router,
};
use px_console_store::{
    ApplicationInstance, OpenResourceSession, ResourceDescriptor, ResourceSession, SessionTarget,
    StartApplication,
};
use std::sync::Arc;
use uuid::Uuid;
use zeroize::Zeroizing;

fn serialize_secret<S>(value: &Zeroizing<String>, serializer: S) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    serializer.serialize_str(value.as_str())
}

#[derive(serde::Serialize)]
struct RdpFrontendBootstrap {
    schema: u32,
    account_name: String,
    domain: String,
    proxy_certificate_sha256: String,
    #[serde(serialize_with = "serialize_secret")]
    password: Zeroizing<String>,
}

#[derive(serde::Serialize)]
struct RelayFrontendBootstrap {
    host: String,
    port: u16,
    admission_ticket: String,
}

#[derive(serde::Serialize)]
struct DescriptorResponse {
    descriptor: ResourceDescriptor,
    #[serde(serialize_with = "serialize_secret")]
    token: Zeroizing<String>,
    relay: Option<RelayFrontendBootstrap>,
    rdp: Option<RdpFrontendBootstrap>,
}

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/instances", get(instances).post(start))
        .route("/api/console/instances/{id}", get(instance))
        .route("/api/console/instances/{id}/stop", post(stop))
        .route("/api/console/resource-sessions", post(open))
        .route("/api/console/resource-sessions/{id}", get(session))
        .route(
            "/api/console/resource-sessions/{id}/descriptor",
            post(descriptor),
        )
        .route("/api/console/resource-sessions/{id}/close", post(close))
        .route(
            "/api/console/managed/resource-sessions",
            get(managed_sessions),
        )
        .route(
            "/api/console/managed/instances/{id}/stop",
            post(managed_stop),
        )
}

async fn instances(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Vec<ApplicationInstance>>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .instances()
            .list_owned(context.credential(), context.client, page.after, page.limit)
            .await?,
    ))
}

async fn start(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(value): Input<StartApplication>,
) -> Result<(StatusCode, Json<ApplicationInstance>), ApiError> {
    let context = request::resource_context(&state, &headers)?;
    let result = state
        .db
        .instances()
        .reserve_with_entitlement(
            context.credential(),
            context.client,
            state.epoch,
            &value,
            state.entitlement(),
        )
        .await?;
    Ok((StatusCode::CREATED, Json(result)))
}

async fn instance(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<ApplicationInstance>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .instances()
            .get(context.credential(), context.client, id)
            .await?,
    ))
}

async fn stop(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(value): Input<Revision>,
) -> Result<Json<ApplicationInstance>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .instances()
            .stop(
                context.credential(),
                context.client,
                state.epoch,
                id,
                value.revision,
            )
            .await?,
    ))
}

async fn managed_stop(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(value): Input<Revision>,
) -> Result<Json<ApplicationInstance>, ApiError> {
    let admin = request::administrator(&state, &headers)?;
    Ok(Json(
        state
            .db
            .instances()
            .stop_managed(&admin, state.epoch, id, value.revision)
            .await?,
    ))
}

async fn open(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(value): Input<OpenResourceSession>,
) -> Result<(StatusCode, Json<ResourceSession>), ApiError> {
    let context = request::resource_context(&state, &headers)?;
    let result = state
        .db
        .resource_sessions()
        .open_with_entitlement(
            context.credential(),
            context.client,
            &value,
            state.entitlement(),
        )
        .await?;
    Ok((StatusCode::CREATED, Json(result)))
}

async fn session(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<ResourceSession>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .resource_sessions()
            .get(context.credential(), context.client, id)
            .await?,
    ))
}

async fn descriptor(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(value): Input<Revision>,
) -> Result<Json<DescriptorResponse>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    let (token, digest) = request::mint();
    let descriptor = state
        .db
        .resource_sessions()
        .descriptor_with_entitlement(
            context.credential(),
            context.client,
            id,
            value.revision,
            &digest,
            state.entitlement(),
        )
        .await?;
    let rdp = if descriptor.transport == "rdp" {
        let domain = descriptor.rdp_domain.clone().ok_or(ApiError::Unavailable)?;
        let proxy_certificate_sha256 = descriptor
            .rdp_proxy_certificate_sha256
            .clone()
            .ok_or(ApiError::Unavailable)?;
        let credential = state
            .db
            .workspaces()
            .credentials_for_frontend(descriptor.session.id)
            .await?;
        Some(RdpFrontendBootstrap {
            schema: 1,
            account_name: credential.account_name,
            domain,
            proxy_certificate_sha256,
            password: credential.password,
        })
    } else {
        None
    };
    let relay = if let Some(endpoint) = state.relay.as_ref() {
        let remote_resource_id = match descriptor.session.target {
            SessionTarget::Desktop { device_id } => device_id,
            SessionTarget::CloudApplication { instance_id, .. } => instance_id,
        };
        let now_unix_seconds =
            u64::try_from(chrono::Utc::now().timestamp()).map_err(|_| ApiError::Unavailable)?;
        let expires_at_unix_seconds =
            u64::try_from(descriptor.expires_at.timestamp()).map_err(|_| ApiError::Unavailable)?;
        let admission_ticket = px_relay_admission::issue(
            endpoint.app_key.as_bytes(),
            descriptor.session.id,
            remote_resource_id,
            now_unix_seconds,
            expires_at_unix_seconds,
        )
        .ok_or(ApiError::Unavailable)?;
        Some(RelayFrontendBootstrap {
            host: endpoint.host.clone(),
            port: endpoint.port,
            admission_ticket: admission_ticket.as_str().to_string(),
        })
    } else {
        None
    };
    Ok(Json(DescriptorResponse {
        descriptor,
        token,
        relay,
        rdp,
    }))
}

async fn close(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(value): Input<Revision>,
) -> Result<Json<ResourceSession>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .resource_sessions()
            .request_close(context.credential(), context.client, id, value.revision)
            .await?,
    ))
}

async fn managed_sessions(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Vec<ResourceSession>>, ApiError> {
    let admin = request::administrator(&state, &headers)?;
    Ok(Json(
        state
            .db
            .resource_sessions()
            .list_managed(&admin, page.after, page.limit)
            .await?,
    ))
}
