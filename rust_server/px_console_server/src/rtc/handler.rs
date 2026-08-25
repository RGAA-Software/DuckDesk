use crate::console_api_error::ConsoleApiError;
use crate::media_sidecar::{apply_turn_config, turn_status, TurnSidecarStatus};
use crate::rtc::model::{RtcIceConfig, RtcIceConfigView};
use crate::{gConsolePanelConnMgr, gConsoleServiceConnMgr, gRtcConfigManager};
use axum::Json;
use px_base::{ok_resp, RespMessage};

pub async fn get_admin_config() -> Result<Json<RespMessage<RtcIceConfig>>, ConsoleApiError> {
    Ok(Json(ok_resp(gRtcConfigManager.config().await)))
}

pub async fn update_admin_config(
    Json(config): Json<RtcIceConfig>,
) -> Result<Json<RespMessage<RtcIceConfig>>, ConsoleApiError> {
    let previous = gRtcConfigManager.config().await;
    let updated = gRtcConfigManager
        .prepare_update(config)
        .await
        .map_err(|error| {
            tracing::warn!(%error, "reject RTC ICE configuration update");
            if error.contains("revision conflict") {
                ConsoleApiError::VersionConflict
            } else {
                ConsoleApiError::InvalidParams
            }
        })?;
    let secret = gRtcConfigManager
        .turn_rest_secret_base64()
        .await
        .map_err(|error| {
            tracing::error!(%error, "TURN REST secret is unavailable");
            ConsoleApiError::InternalError
        })?;
    let storage_dir = gRtcConfigManager.storage_dir().await.map_err(|error| {
        tracing::error!(%error, "RTC storage is unavailable");
        ConsoleApiError::InternalError
    })?;
    if let Err(error) = apply_turn_config(
        &updated.managed_console_server,
        updated.revision,
        &secret,
        &storage_dir,
        true,
    )
    .await
    {
        tracing::error!(%error, "new managed Coturn configuration failed; restoring previous configuration");
        if let Err(rollback_error) = apply_turn_config(
            &previous.managed_console_server,
            previous.revision,
            &secret,
            &storage_dir,
            true,
        )
        .await
        {
            tracing::error!(%rollback_error, "managed Coturn rollback failed");
        }
        return Err(ConsoleApiError::InternalError);
    }
    if let Err(error) = gRtcConfigManager.commit(updated.clone()).await {
        tracing::error!(%error, "persist RTC ICE configuration failed; restoring previous Coturn configuration");
        let _ = apply_turn_config(
            &previous.managed_console_server,
            previous.revision,
            &secret,
            &storage_dir,
            true,
        )
        .await;
        return Err(ConsoleApiError::InternalError);
    }
    let changed_at = px_base::get_current_timestamp();
    let (panels, services) = tokio::join!(
        gConsolePanelConnMgr.broadcast_rtc_ice_config_changed(updated.revision, changed_at),
        gConsoleServiceConnMgr.broadcast_rtc_ice_config_changed(updated.revision, changed_at),
    );
    tracing::info!(
        revision = updated.revision,
        panels,
        services,
        "RTC ICE configuration invalidation delivered"
    );
    Ok(Json(ok_resp(updated)))
}

pub async fn validate_admin_config(
    Json(mut config): Json<RtcIceConfig>,
) -> Result<Json<RespMessage<bool>>, ConsoleApiError> {
    config.normalize().map_err(|error| {
        tracing::warn!(%error, "RTC ICE configuration validation failed");
        ConsoleApiError::InvalidParams
    })?;
    Ok(Json(ok_resp(true)))
}

pub async fn get_node_config() -> Result<Json<RespMessage<RtcIceConfigView>>, ConsoleApiError> {
    Ok(Json(ok_resp(gRtcConfigManager.public_config().await)))
}

pub async fn get_turn_status() -> Result<Json<RespMessage<TurnSidecarStatus>>, ConsoleApiError> {
    Ok(Json(ok_resp(turn_status())))
}
