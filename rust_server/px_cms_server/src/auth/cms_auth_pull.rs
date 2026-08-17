//! CMS 设备主动上报 + 拉取授权（网络上报授权模式，取代手工粘贴 license）。
//!
//! 与 gopico/clientbox/goagent 三端语义一致：
//! - 启动时及周期性向 auth server `POST /api/v1/device/pull` 上报机器码
//!   （product=Pixels_cms），未知设备自动注册为试用授权；
//! - 拉取到的签名 license 复用现有验签/落库链路
//!   （LicenseVerifier → license_to_authorization → KvStorage → AuthManager）；
//! - 响应 revoked=true 时清空本地授权（内存 + KvStorage 缓存）；
//! - 任何网络/HTTP/验签失败只记日志，不动本地已有授权（沿用缓存，服务不中断）。

use crate::auth::cms_auth_license_keys::{init_license_verifier, license_to_authorization};
use crate::auth::cms_auth_manager::KEY_AUTHORIZATION;
use crate::cms_settings::DEFAULT_AUTH_PULL_INTERVAL_SECS;
use crate::{gAuthManager, gKvStorage, gLicenseVerifier, gCmsContext, gCmsDatabase, gCmsSettings};
use px_auth_mgr::app_credential as cred;
use px_auth_mgr::app_secret_util::calculate_app_secret;
use px_auth_mgr::auth_license::{LicenseVerifier, SignedLicense};
use px_auth_mgr::authorization::{Authorization, PRODUCT_Pixels_CMS};
use px_base::{get_current_timestamp, RespMessage};
use mongodb::bson::doc;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use std::time::Duration;

const PULL_PATH: &str = "/api/v1/device/pull";
const PULL_TIMEOUT_SECS: u64 = 15;

#[derive(Debug, Serialize)]
struct DevicePullRequest {
    product: String,
    device_code: String,
    client_version: String,
    client_status: String,
    os: String,
    device_count: i32,
}

/// 对齐 auth server `authorization_handler.rs` 的 DevicePullResponse。
/// （Serialize 仅为满足 `RespMessage<T>` 的 T: Serialize + Default 约束。）
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct DevicePullResponse {
    #[serde(default)]
    pub deploy_str: String,
    #[serde(default)]
    pub auth_id: String,
    #[serde(default)]
    pub mode: String,
    #[serde(default)]
    pub days: i32,
    #[serde(default)]
    pub max_devices: i32,
    #[serde(default)]
    pub role: i32,
    #[serde(default)]
    pub expires_at_ms: i64,
    #[serde(default)]
    pub revoked: bool,
    #[serde(default)]
    pub registered_new: bool,
    #[serde(default)]
    pub server_time_ms: i64,
    #[serde(default)]
    pub last_modify_timestamp: i64,
    /// 服务器口径的已使用时间（客户端直接采用，不做本地计时）。
    #[serde(default)]
    pub used_time_ms: i64,
}

/// 一次 pull 的结果。
pub enum PullOutcome {
    /// 拉取到有效授权并已落库（试用或正式）。
    Active(Authorization),
    /// 服务器已吊销该设备授权，本地授权已清空。
    Revoked,
}

/// 拉取一次授权。失败返回 Err（本地授权保持不变）。
pub async fn pull_once() -> Result<PullOutcome, String> {
    let (base_url, appkey, app_secret) = {
        let st = gCmsSettings.lock().await;
        (
            st.auth_server_url.clone(),
            st.app_credential.appkey.clone(),
            st.app_credential.app_secret.clone(),
        )
    };
    if base_url.trim().is_empty() {
        return Err("auth_server_url is not configured".to_string());
    }

    let machine_code = gCmsContext.lock().await.machine_code.clone();
    if machine_code.is_empty() {
        return Err("machine code is empty".to_string());
    }

    let request = DevicePullRequest {
        product: PRODUCT_Pixels_CMS.to_string(),
        device_code: machine_code,
        client_version: env!("CARGO_PKG_VERSION").to_string(),
        client_status: "ok".to_string(),
        os: std::env::consts::OS.to_string(),
        device_count: query_device_count().await,
    };
    let body = serde_json::to_string(&request).map_err(|e| format!("serialize body: {e}"))?;

    let url = format!("{}{}", base_url.trim_end_matches('/'), PULL_PATH);
    let client = reqwest::Client::builder()
        .danger_accept_invalid_certs(true) // auth server 使用自签名证书
        .timeout(Duration::from_secs(PULL_TIMEOUT_SECS))
        .build()
        .map_err(|e| format!("build http client: {e}"))?;

    let mut req = client.post(&url).body(body.clone());
    // appkey/app_secret 为空时不带签名头直接发（灰度期）；
    // 非空则带 HMAC 签名头（与 auth server require_app_credential=true 对应）。
    if !appkey.is_empty() && !app_secret.is_empty() {
        let ts = get_current_timestamp();
        let sign = cred::sign(&appkey, &app_secret, ts, body.as_bytes())
            .ok_or_else(|| "failed to sign app credential (bad app_secret hex?)".to_string())?;
        req = req
            .header(cred::HEADER_APP_KEY, appkey.as_str())
            .header(cred::HEADER_APP_TIMESTAMP, ts.to_string())
            .header(cred::HEADER_APP_SIGN, sign);
    }

    let resp = req
        .send()
        .await
        .map_err(|e| format!("POST {url} failed: {e}"))?;
    let status = resp.status();
    let text = resp
        .text()
        .await
        .map_err(|e| format!("read response body: {e}"))?;
    if !status.is_success() {
        return Err(format!("POST {url} returned {status}: {text}"));
    }
    let msg: RespMessage<DevicePullResponse> =
        serde_json::from_str(&text).map_err(|e| format!("parse response: {e}"))?;
    if msg.code != 200 {
        return Err(format!("auth server rejected pull: code={} msg={}", msg.code, msg.message));
    }
    let data = msg.data;

    if data.revoked {
        tracing::warn!(
            "auth pull: authorization revoked by server, auth_id='{}', clearing local authorization",
            data.auth_id
        );
        clear_local_authorization().await;
        return Ok(PullOutcome::Revoked);
    }

    let auth = apply_deploy_string(&data.deploy_str, &data.mode).await?;
    // 已使用时间直接采用服务器计算值（周期 pull 刷新；网络失败沿用上一次）。
    gAuthManager
        .lock()
        .await
        .update_server_used_time(data.used_time_ms)
        .await;
    tracing::info!(
        "auth pull: OK, auth_id='{}' mode='{}' days={} max_streams={} registered_new={}",
        auth.auth_id,
        auth.mode,
        auth.days,
        auth.max_streams,
        data.registered_new
    );
    Ok(PullOutcome::Active(auth))
}

/// 启动周期 pull 任务：立即 pull 一次，之后每 auth_pull_interval_secs 一次。
pub async fn start_pull_loop() {
    let (base_url, interval_secs) = {
        let st = gCmsSettings.lock().await;
        (st.auth_server_url.clone(), st.auth_pull_interval_secs)
    };
    if base_url.trim().is_empty() {
        tracing::warn!("auth pull loop not started: auth_server_url is not configured");
        return;
    }
    let interval_secs = if interval_secs == 0 {
        DEFAULT_AUTH_PULL_INTERVAL_SECS
    } else {
        interval_secs
    };
    tokio::spawn(async move {
        loop {
            if let Err(e) = pull_once().await {
                // 失败只记日志，本地授权保持不变（沿用缓存）。
                tracing::warn!("auth pull failed (keeping local authorization): {}", e);
            }
            tokio::time::sleep(Duration::from_secs(interval_secs)).await;
        }
    });
    tracing::info!("auth pull loop started, interval={}s, url={}", interval_secs, base_url);
}

/// 验签 deploy string 并落库（KvStorage 缓存 + AuthManager）。
/// mode 来自 pull 响应（license 签名负载里不含 mode，缺失时按 licensed 处理）。
/// 失败返回 Err，本地已有授权保持不变。
async fn apply_deploy_string(deploy_str: &str, mode: &str) -> Result<Authorization, String> {
    let verifier = ensure_license_verifier().await?;
    let machine_code = gCmsContext.lock().await.machine_code.clone();
    let now_ms = get_current_timestamp();

    let signed = SignedLicense::parse_deploy_string(deploy_str)
        .map_err(|e| format!("parse_deploy_string failed: {e}"))?;

    let verify_result = verifier
        .verify(&signed, &machine_code, now_ms)
        .map_err(|e| format!("verify error: {e}"))?;
    if !verify_result {
        let sig_ok = verifier.verify_signature(&signed).unwrap_or(false);
        tracing::error!(
            "auth pull: license verify failed — signature_ok={} machine_code_ok={} \
             (license='{}' vs local='{}') expiry_ok={}",
            sig_ok,
            signed.license.machine_code == machine_code,
            signed.license.machine_code,
            machine_code,
            signed.license.expires_at_ms > now_ms
        );
        return Err("license verification failed".to_string());
    }

    let existing = gAuthManager.lock().await.get_auth().await;
    let mut auth = license_to_authorization(&signed.license, Some(&existing), deploy_str.to_string());

    // mode 不在 license 签名负载里，由 pull 响应携带；空（异常）时保持 licensed。
    if !mode.is_empty() {
        auth.mode = mode.to_string();
    }

    // Derive app_secret from appkey so the appkey filter keeps working.
    auth.app_secret = calculate_app_secret(auth.appkey.clone());

    // save to db (KvStorage 未初始化时 put 返回 false，如面板进程，属正常降级)
    if !gKvStorage.lock().await.put(KEY_AUTHORIZATION, deploy_str) {
        tracing::warn!("auth pull: KvStorage put failed (not initialized?), authorization kept in memory only");
    }

    // update auth manager
    gAuthManager.lock().await.update_auth(auth.clone()).await;

    // used time
    auth.used_time_ms = gAuthManager.lock().await.get_used_time().await;
    Ok(auth)
}

/// 服务器吊销后清空本地授权（AuthManager 置空 + 删除 KvStorage 缓存）。
async fn clear_local_authorization() {
    let mgr = gAuthManager.lock().await;
    mgr.update_auth(Authorization::default()).await;
    mgr.update_server_used_time(0).await;
    gKvStorage.lock().await.del(KEY_AUTHORIZATION);
}

/// 取 license verifier；未初始化时（如面板进程）按需懒加载。
async fn ensure_license_verifier() -> Result<Arc<LicenseVerifier>, String> {
    let mut guard = gLicenseVerifier.lock().await;
    if guard.is_none() {
        match init_license_verifier() {
            Ok(verifier) => *guard = Some(Arc::new(verifier)),
            Err(e) => return Err(format!("license verifier init failed: {e}")),
        }
    }
    Ok(guard.as_ref().map(Arc::clone).unwrap())
}

/// 设备数上报（best-effort；数据库未初始化或查询失败时为 0）。
async fn query_device_count() -> i32 {
    let c_device = gCmsDatabase.lock().await.c_device.clone();
    match c_device {
        Some(c) => c
            .lock()
            .await
            .count_documents(doc! {})
            .await
            .map(|n| n as i32)
            .unwrap_or(0),
        None => 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn device_pull_response_deserializes_full_payload() {
        let text = r#"{
            "code": 200,
            "message": "ok",
            "timestamp": 1700000000000,
            "data": {
                "deploy_str": "deploy.abc",
                "auth_id": "auth-1",
                "mode": "trial",
                "days": 365000,
                "max_devices": 1,
                "role": 1,
                "expires_at_ms": 1700000001000,
                "revoked": false,
                "registered_new": true,
                "server_time_ms": 1700000000000,
                "last_modify_timestamp": 1700000000000
            }
        }"#;
        let msg: RespMessage<DevicePullResponse> = serde_json::from_str(text).unwrap();
        assert_eq!(msg.code, 200);
        assert_eq!(msg.data.auth_id, "auth-1");
        assert_eq!(msg.data.mode, "trial");
        assert!(msg.data.registered_new);
        assert!(!msg.data.revoked);
    }

    #[test]
    fn device_pull_response_defaults_on_sparse_payload() {
        let text = r#"{"code":200,"message":"ok","timestamp":1,"data":{"revoked":true}}"#;
        let msg: RespMessage<DevicePullResponse> = serde_json::from_str(text).unwrap();
        assert!(msg.data.revoked);
        assert!(msg.data.deploy_str.is_empty());
        assert_eq!(msg.data.days, 0);
    }
}
