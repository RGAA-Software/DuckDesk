use crate::auth::spvr_auth_license_keys::parse_and_verify_signed_license;
use crate::{gKvStorage, gLicenseVerifier, gSpvrContext};
use gr_auth_mgr::app_secret_util::calculate_app_secret;
use gr_auth_mgr::authorization::Authorization;
use gr_base::get_current_timestamp;
use std::sync::Arc;
use tokio::sync::Mutex;

pub const KEY_AUTHORIZATION: &str = "authorization";

pub struct AuthManager {
    auth: Arc<Mutex<Authorization>>,
    /// 最近一次 pull 时服务器返回的已使用时间（服务器口径；未拉取过为 0）。
    server_used_time_ms: Arc<Mutex<i64>>,
}

impl AuthManager {
    pub fn new() -> AuthManager {
        AuthManager {
            auth: Default::default(),
            server_used_time_ms: Arc::new(Mutex::new(0)),
        }
    }

    /// Loads the authorization from the KvStorage cache (deploy string written
    /// by the auth pull flow). Only signed license format is accepted.
    /// Returns `false` if no valid authorization is found; the server still
    /// starts — the background pull loop will fetch the authorization from
    /// the auth server (网络上报授权模式，不再支持本地 license 文件)。
    pub async fn load(&mut self) -> bool {
        let auth_str = match gKvStorage.lock().await.get(KEY_AUTHORIZATION) {
            Some(str) if !str.trim().is_empty() => {
                tracing::info!("load: found cached authorization in KvStorage (len={})", str.len());
                str
            }
            _ => {
                tracing::info!("load: no cached authorization in KvStorage; starting unlicensed");
                return false;
            }
        };

        let machine_code = gSpvrContext.lock().await.machine_code.clone();
        let now_ms = get_current_timestamp();
        tracing::info!(
            "load: attempting to parse signed license, machine_code='{}' now_ms={}",
            machine_code,
            now_ms
        );

        let Some(verifier) = gLicenseVerifier
            .lock()
            .await
            .as_ref()
            .map(Arc::clone)
        else {
            tracing::error!("load: license verifier not initialized");
            return false;
        };

        match parse_and_verify_signed_license(&verifier, &auth_str, &machine_code, now_ms) {
            Ok(auth) => {
                tracing::info!(
                    "load: signed auth loaded OK, auth_id='{}' auth_name='{}' \
                     machine_code='{}' appkey='{}' days={} max_streams={} username='{}' \
                     password_len={}",
                    auth.auth_id,
                    auth.auth_name,
                    auth.machine_code,
                    auth.appkey,
                    auth.days,
                    auth.max_streams,
                    auth.username,
                    auth.password.len()
                );
                self.update_auth(auth).await;
                true
            }
            Err(e) => {
                tracing::error!(
                    "load: failed to parse/verify signed license: {}. \
                     auth_str preview='{}'",
                    e,
                    &auth_str[..auth_str.len().min(80)]
                );
                false
            }
        }
    }

    pub async fn get_auth(&self) -> Authorization {
        self.auth.lock().await.clone()
    }

    pub async fn update_auth(&self, auth: Authorization) {
        let mut auth_guard = self.auth.lock().await;
        *auth_guard = auth;
    }

    pub async fn verify_appkey(&self, appkey: String) -> bool {
        if appkey.is_empty() {
            return false;
        }
        let app_secret = calculate_app_secret(appkey.clone());
        self.auth.lock().await.appkey == appkey && self.auth.lock().await.app_secret == app_secret
    }

    /// 已使用时间（毫秒）：**直接采用授权服务器计算并通过 pull 返回的值**，
    /// 本地不做计时也不做时钟纠正。周期 pull 自动刷新；网络失败时沿用上一次
    /// 拉到的值继续运行，只有 license 本身明确过期（end_timestamp_ms）才失效。
    pub async fn get_used_time(&self) -> i64 {
        let auth = self.get_auth().await;
        if auth.auth_id.is_empty() {
            return 0;
        }
        *self.server_used_time_ms.lock().await
    }

    /// 记录服务器返回的已使用时间（pull 成功后调用；吊销清零）。
    pub async fn update_server_used_time(&self, used_time_ms: i64) {
        *self.server_used_time_ms.lock().await = used_time_ms;
    }

    pub async fn is_auth_ok(&self, auth_id: String, auth_password: String) -> bool {
        let self_auth_id = self.auth.lock().await.auth_id.clone();
        let self_auth_password = self.auth.lock().await.password.clone();
        auth_id == self_auth_id && auth_password == self_auth_password
    }
}
