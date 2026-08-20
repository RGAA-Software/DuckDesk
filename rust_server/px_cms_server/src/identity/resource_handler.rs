use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::{AppAccessMode, AppInstance, InstanceState, StartInstanceReq};
use crate::cms_api_error::CmsApiError;
use crate::event::audit;
use crate::gCmsUserDeviceMgr;
use crate::identity::manager::IdentityManager;
use crate::user::session::{AuthenticatedGuest, AuthenticatedUser};
use axum::extract::{Extension, Path};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default, Serialize)]
pub struct RunningInstanceSummary {
    pub instance_id: String,
    pub state: String,
    pub reconnectable: bool,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct ApplicationCard {
    pub app_id: String,
    pub name: String,
    pub access_mode: AppAccessMode,
    pub cover_url: String,
    pub running_instance: Option<RunningInstanceSummary>,
    pub version: i64,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct InstanceView {
    pub instance_id: String,
    pub app_id: String,
    pub app_name: String,
    pub state: String,
    pub created_at: i64,
    pub started_at: Option<i64>,
    pub stopped_at: Option<i64>,
    pub error_code: Option<String>,
    pub reconnectable: bool,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct ResourceSummary {
    pub device_count: usize,
    pub application_count: usize,
    pub active_instance_count: usize,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct StartApplicationRequest {
    pub client_nonce: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct StopInstanceRequest {
    pub reason: Option<String>,
}

fn state_name(state: &InstanceState) -> String {
    match state {
        InstanceState::Starting => "starting",
        InstanceState::Running => "running",
        InstanceState::Failed => "failed",
        InstanceState::Stopping => "stopping",
        InstanceState::Stopped => "stopped",
    }
    .to_string()
}

fn usage_ms_since(instances: &[AppInstance], since_ms: i64, now_ms: i64) -> i64 {
    instances
        .iter()
        .map(|instance| {
            let start = instance
                .started_at_ms
                .max(instance.created_at_ms)
                .max(since_ms);
            let end = if instance.stopped_at_ms > 0 {
                instance.stopped_at_ms.min(now_ms)
            } else if matches!(
                instance.state,
                InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
            ) {
                now_ms
            } else {
                start
            };
            (end - start).max(0)
        })
        .sum()
}

fn local_day_start_ms() -> i64 {
    use chrono::TimeZone;
    let now = chrono::Local::now();
    now.timezone()
        .from_local_datetime(
            &now.date_naive()
                .and_hms_opt(0, 0, 0)
                .expect("valid midnight"),
        )
        .earliest()
        .map(|value| value.timestamp_millis())
        .unwrap_or_else(|| now.timestamp_millis())
}

async fn instance_view(instance: AppInstance) -> InstanceView {
    let app_name = gAppScheduleManager
        .get_application(&instance.app_id)
        .await
        .map(|app| app.name)
        .unwrap_or_default();
    let reconnectable = instance.state == InstanceState::Running;
    InstanceView {
        instance_id: instance.instance_id,
        app_id: instance.app_id,
        app_name,
        state: state_name(&instance.state),
        created_at: instance.created_at_ms,
        started_at: (instance.started_at_ms > 0).then_some(instance.started_at_ms),
        stopped_at: (instance.stopped_at_ms > 0).then_some(instance.stopped_at_ms),
        error_code: (!instance.error.is_empty()).then_some(instance.error),
        reconnectable,
    }
}

async fn authorized_apps(uid: &str) -> Result<Vec<ApplicationCard>, CmsApiError> {
    let acl_ids = IdentityManager::authorized_app_ids(uid).await?;
    let instances = gAppScheduleManager
        .list_instances_for_owner("user", uid)
        .await;
    let mut cards = Vec::new();
    for app in gAppScheduleManager.list_applications().await {
        if app.access_mode == AppAccessMode::Acl && !acl_ids.contains(&app.app_id) {
            continue;
        }
        let running_instance = instances
            .iter()
            .find(|instance| {
                instance.app_id == app.app_id
                    && matches!(
                        instance.state,
                        InstanceState::Starting | InstanceState::Running
                    )
            })
            .map(|instance| RunningInstanceSummary {
                instance_id: instance.instance_id.clone(),
                state: state_name(&instance.state),
                reconnectable: instance.state == InstanceState::Running,
            });
        cards.push(ApplicationCard {
            app_id: app.app_id,
            name: app.name,
            access_mode: app.access_mode,
            cover_url: String::new(),
            running_instance,
            version: app.version,
        });
    }
    cards.sort_by(|left, right| left.name.cmp(&right.name));
    Ok(cards)
}

pub async fn list_user_apps(
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<Vec<ApplicationCard>>>, CmsApiError> {
    Ok(Json(ok_resp(authorized_apps(&subject.uid).await?)))
}

pub async fn start_user_app(
    Path(app_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<StartApplicationRequest>,
) -> Result<Json<RespMessage<InstanceView>>, CmsApiError> {
    if request.client_nonce.is_empty()
        || request.client_nonce.len() > 128
        || request.client_nonce.chars().any(char::is_control)
    {
        return Err(CmsApiError::InvalidParams);
    }
    let app = gAppScheduleManager
        .get_application(&app_id)
        .await
        .ok_or(CmsApiError::ResourceNotFound)?;
    let authorized = app.access_mode == AppAccessMode::Public
        || IdentityManager::authorized_app_ids(&subject.uid)
            .await?
            .contains(&app_id);
    if !authorized {
        return Err(CmsApiError::ResourceNotFound);
    }
    if let Some(existing) = gAppScheduleManager
        .list_instances_for_owner("user", &subject.uid)
        .await
        .into_iter()
        .find(|instance| {
            instance.app_id == app_id
                && instance.owner_session_id == subject.sid
                && instance.client_nonce == request.client_nonce
                && matches!(
                    instance.state,
                    InstanceState::Starting | InstanceState::Running
                )
        })
    {
        return Ok(Json(ok_resp(instance_view(existing).await)));
    }
    let settings = crate::gCmsSettings.lock().await.user.clone();
    crate::user::rate_limit::check(
        format!("start:user:{}", subject.uid),
        settings.rate_limit.start_per_subject_per_minute,
        60 * 1000,
    )?;
    let active_count = gAppScheduleManager
        .list_instances_for_owner("user", &subject.uid)
        .await
        .into_iter()
        .filter(|instance| {
            matches!(
                instance.state,
                InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
            )
        })
        .count();
    if active_count >= settings.quota.user_concurrent_instances {
        return Err(CmsApiError::QuotaExceeded);
    }
    let client_key = format!("{}:{}", subject.sid, request.client_nonce);
    let instance = gAppScheduleManager
        .start_instance_owned(
            StartInstanceReq {
                app_id,
                device_id: None,
                listen_port: None,
                client_key: Some(client_key),
                client_key_permanent: true,
            },
            "user",
            &subject.uid,
            &subject.sid,
            &request.client_nonce,
        )
        .await
        .map_err(|error| {
            tracing::warn!("user app start rejected: {}", error);
            CmsApiError::VersionConflict
        })?;
    audit::record(
        "user",
        &subject.uid,
        "app_start",
        "success",
        "app_instance",
        &instance.instance_id,
        "",
    )
    .await;
    Ok(Json(ok_resp(instance_view(instance).await)))
}

pub async fn list_user_instances(
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<Vec<InstanceView>>>, CmsApiError> {
    let instances = gAppScheduleManager
        .list_instances_for_owner("user", &subject.uid)
        .await;
    let mut views = Vec::with_capacity(instances.len());
    for instance in instances {
        views.push(instance_view(instance).await);
    }
    Ok(Json(ok_resp(views)))
}

pub async fn stop_user_instance(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<StopInstanceRequest>,
) -> Result<Json<RespMessage<InstanceView>>, CmsApiError> {
    if request
        .reason
        .as_deref()
        .is_some_and(|reason| reason.len() > 256 || reason.chars().any(char::is_control))
    {
        return Err(CmsApiError::InvalidParams);
    }
    let instance = gAppScheduleManager
        .get_instance(&instance_id)
        .await
        .filter(|instance| instance.owner_type == "user" && instance.owner_id == subject.uid)
        .ok_or(CmsApiError::ResourceNotFound)?;
    let stopped = gAppScheduleManager
        .stop_instance(&instance.instance_id)
        .await
        .map_err(|_| CmsApiError::VersionConflict)?;
    audit::record(
        "user",
        &subject.uid,
        "app_stop",
        "success",
        "app_instance",
        &instance.instance_id,
        request.reason.as_deref().unwrap_or(""),
    )
    .await;
    Ok(Json(ok_resp(instance_view(stopped).await)))
}

pub async fn user_resource_summary(
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<ResourceSummary>>, CmsApiError> {
    let devices = gCmsUserDeviceMgr
        .query_user_device_summaries(subject.uid.clone())
        .await?;
    let apps = authorized_apps(&subject.uid).await?;
    let active_instance_count = gAppScheduleManager
        .list_instances_for_owner("user", &subject.uid)
        .await
        .into_iter()
        .filter(|instance| {
            matches!(
                instance.state,
                InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
            )
        })
        .count();
    Ok(Json(ok_resp(ResourceSummary {
        device_count: devices.len(),
        application_count: apps.len(),
        active_instance_count,
    })))
}

async fn public_apps_for_guest(subject: &AuthenticatedGuest) -> Vec<ApplicationCard> {
    let instances = gAppScheduleManager
        .list_instances_for_owner("guest", &subject.guest_id)
        .await;
    let mut cards = Vec::new();
    for app in gAppScheduleManager.list_applications().await {
        if app.access_mode != AppAccessMode::Public {
            continue;
        }
        let running_instance = instances
            .iter()
            .find(|instance| {
                instance.app_id == app.app_id
                    && matches!(
                        instance.state,
                        InstanceState::Starting | InstanceState::Running
                    )
            })
            .map(|instance| RunningInstanceSummary {
                instance_id: instance.instance_id.clone(),
                state: state_name(&instance.state),
                reconnectable: instance.state == InstanceState::Running,
            });
        cards.push(ApplicationCard {
            app_id: app.app_id,
            name: app.name,
            access_mode: app.access_mode,
            cover_url: String::new(),
            running_instance,
            version: app.version,
        });
    }
    cards.sort_by(|left, right| left.name.cmp(&right.name));
    cards
}

pub async fn list_public_apps(
    Extension(subject): Extension<AuthenticatedGuest>,
) -> Result<Json<RespMessage<Vec<ApplicationCard>>>, CmsApiError> {
    Ok(Json(ok_resp(public_apps_for_guest(&subject).await)))
}

pub async fn start_public_app(
    Path(app_id): Path<String>,
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<StartApplicationRequest>,
) -> Result<Json<RespMessage<InstanceView>>, CmsApiError> {
    if request.client_nonce.is_empty()
        || request.client_nonce.len() > 128
        || request.client_nonce.chars().any(char::is_control)
    {
        return Err(CmsApiError::InvalidParams);
    }
    let app = gAppScheduleManager
        .get_application(&app_id)
        .await
        .filter(|app| app.access_mode == AppAccessMode::Public)
        .ok_or(CmsApiError::ResourceNotFound)?;
    if let Some(existing) = gAppScheduleManager
        .list_instances_for_owner("guest", &subject.guest_id)
        .await
        .into_iter()
        .find(|instance| {
            instance.app_id == app_id
                && instance.owner_session_id == subject.sid
                && instance.client_nonce == request.client_nonce
                && matches!(
                    instance.state,
                    InstanceState::Starting | InstanceState::Running
                )
        })
    {
        return Ok(Json(ok_resp(instance_view(existing).await)));
    }
    let settings = crate::gCmsSettings.lock().await.user.clone();
    crate::user::rate_limit::check(
        format!("start:guest:{}", subject.guest_id),
        settings.rate_limit.start_per_subject_per_minute,
        60 * 1000,
    )?;
    let all = gAppScheduleManager.list_instances().await;
    let active = |instance: &&AppInstance| {
        matches!(
            instance.state,
            InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
        )
    };
    if all
        .iter()
        .filter(|instance| instance.owner_type == "guest" && instance.owner_id == subject.guest_id)
        .filter(active)
        .count()
        >= settings.quota.guest_concurrent_instances
    {
        return Err(CmsApiError::QuotaExceeded);
    }
    let now = px_base::get_current_timestamp();
    let guest_history: Vec<_> = all
        .iter()
        .filter(|instance| instance.owner_type == "guest" && instance.owner_id == subject.guest_id)
        .cloned()
        .collect();
    let used_minutes = usage_ms_since(&guest_history, local_day_start_ms(), now) / 60_000;
    if used_minutes >= settings.quota.guest_daily_minutes as i64 {
        audit::record(
            "guest",
            &subject.guest_id,
            "app_start",
            "failure",
            "application",
            &app_id,
            "daily_quota_exceeded",
        )
        .await;
        return Err(CmsApiError::QuotaExceeded);
    }
    if all
        .iter()
        .filter(|instance| instance.app_id == app.app_id)
        .filter(active)
        .count()
        >= settings.quota.public_app_global_concurrency
    {
        return Err(CmsApiError::QuotaExceeded);
    }
    let instance = gAppScheduleManager
        .start_instance_owned(
            StartInstanceReq {
                app_id,
                device_id: None,
                listen_port: None,
                client_key: Some(format!("{}:{}", subject.sid, request.client_nonce)),
                client_key_permanent: true,
            },
            "guest",
            &subject.guest_id,
            &subject.sid,
            &request.client_nonce,
        )
        .await
        .map_err(|_| CmsApiError::VersionConflict)?;
    audit::record(
        "guest",
        &subject.guest_id,
        "app_start",
        "success",
        "app_instance",
        &instance.instance_id,
        "",
    )
    .await;
    Ok(Json(ok_resp(instance_view(instance).await)))
}

pub async fn list_guest_instances(
    Extension(subject): Extension<AuthenticatedGuest>,
) -> Result<Json<RespMessage<Vec<InstanceView>>>, CmsApiError> {
    let mut views = Vec::new();
    for instance in gAppScheduleManager
        .list_instances_for_owner("guest", &subject.guest_id)
        .await
    {
        views.push(instance_view(instance).await);
    }
    Ok(Json(ok_resp(views)))
}

pub async fn stop_guest_instance(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<StopInstanceRequest>,
) -> Result<Json<RespMessage<InstanceView>>, CmsApiError> {
    if request
        .reason
        .as_deref()
        .is_some_and(|reason| reason.len() > 256 || reason.chars().any(char::is_control))
    {
        return Err(CmsApiError::InvalidParams);
    }
    let instance = gAppScheduleManager
        .get_instance(&instance_id)
        .await
        .filter(|instance| instance.owner_type == "guest" && instance.owner_id == subject.guest_id)
        .ok_or(CmsApiError::ResourceNotFound)?;
    let stopped = gAppScheduleManager
        .stop_instance(&instance.instance_id)
        .await
        .map_err(|_| CmsApiError::VersionConflict)?;
    audit::record(
        "guest",
        &subject.guest_id,
        "app_stop",
        "success",
        "app_instance",
        &instance.instance_id,
        request.reason.as_deref().unwrap_or(""),
    )
    .await;
    Ok(Json(ok_resp(instance_view(stopped).await)))
}
