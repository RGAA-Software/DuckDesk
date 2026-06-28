use crate::auth::spvr_auth_license_keys::{
    is_offline_grace_period_exceeded, parse_and_verify_signed_license,
    verify_license_online, KEY_LAST_ONLINE_VERIFY_MS,
};
use crate::{gAuthManager, gKvStorage, gLicenseVerifier, gSpvrContext};
use gr_auth_mgr::app_secret_util::calculate_app_secret;
use gr_auth_mgr::auth_used_time::{sign_used_time, verify_used_time};
use gr_auth_mgr::auth_util::{parse_authorization, verify_authorization};
use gr_auth_mgr::authorization::Authorization;
use gr_base::{get_current_timestamp, md5_hex};
use std::fs::File;
use std::io::Write;
use std::sync::Arc;
use tokio::sync::Mutex;

pub const KEY_AUTHORIZATION: &str = "authorization";

pub struct AuthManager {
    auth: Arc<Mutex<Authorization>>,
    key_used_time: String,
}

impl AuthManager {
    pub fn new() -> AuthManager {
        AuthManager {
            auth: Default::default(),
            key_used_time: "".to_string(),
        }
    }

    pub async fn load(&mut self) -> bool {
        let auth_str = if let Some(str) = gKvStorage.lock().await.get(KEY_AUTHORIZATION) {
            str
        } else {
            
            std::fs::read_to_string("auth/auth.info").expect("can't read auth/auth.info")
        };

        let machine_code = gSpvrContext.lock().await.machine_code.clone();
        let now_ms = get_current_timestamp();

        // Try the new signed license format first.
        if let Some(verifier) = gLicenseVerifier.lock().await.as_ref() {
            match parse_and_verify_signed_license(verifier, &auth_str, &machine_code, now_ms) {
                Ok(mut auth) => {
                    tracing::info!("signed auth loaded: auth_id={}, auth_name={}, machine_code={}, days={}, max_streams={}",
                        auth.auth_id, auth.auth_name, auth.machine_code, auth.days, auth.max_streams);
                    // Preserve credentials from any previously stored authorization.
                    let existing = self.get_auth().await;
                    if !existing.username.is_empty() {
                        auth.username = existing.username;
                    }
                    if !existing.password.is_empty() {
                        auth.password = existing.password;
                    }
                    if !existing.app_secret.is_empty() {
                        auth.app_secret = existing.app_secret;
                    }
                    self.update_key_used_time(&auth_str);
                    self.update_auth(auth).await;
                    return true;
                }
                Err(e) => {
                    tracing::debug!("signed license parse failed (will try legacy AES): {}", e);
                }
            }
        }

        // Fall back to legacy AES deploy string (read-only, deprecated).
        let auth = parse_authorization(auth_str.clone());
        if let Err(err) = auth {
            tracing::error!("parse auth error: {}, auth: {}", err, auth_str);
            return false;
        }
        let auth = auth.unwrap();
        if let Err(e) = verify_authorization(&auth) {
            tracing::error!("invalid authorization: {}", e);
            return false;
        }

        tracing::warn!("loaded legacy AES authorization; this format is deprecated and will be removed. auth_id={}, auth_name={}",
            auth.auth_id, auth.auth_name);

        self.update_key_used_time(&auth_str);
        self.update_auth(auth.clone()).await;

        // Online/offline hybrid check.
        if let Err(e) = self.perform_online_license_check(&auth, &auth_str).await {
            tracing::error!("license online verification failed: {}", e);
            return false;
        }

        true
    }

    async fn perform_online_license_check(
        &self,
        auth: &Authorization,
        deploy_str: &str,
    ) -> Result<(), String> {
        let now_ms = get_current_timestamp();
        let last_online = gKvStorage
            .lock()
            .await
            .get(KEY_LAST_ONLINE_VERIFY_MS)
            .and_then(|s| s.parse::<i64>().ok())
            .unwrap_or(0);

        // If we are within the offline grace period, skip the network call.
        if !is_offline_grace_period_exceeded(last_online, now_ms) {
            return Ok(());
        }

        match verify_license_online(&auth.verify_server, deploy_str).await {
            Ok(true) => {
                gKvStorage
                    .lock()
                    .await
                    .put(KEY_LAST_ONLINE_VERIFY_MS, now_ms.to_string().as_str());
                tracing::info!("online license verification succeeded");
                Ok(())
            }
            Ok(false) => Err("license rejected by online verification".to_string()),
            Err(e) => {
                // Network/auth-server failure: allow running if we have a previous successful check.
                if last_online > 0 {
                    tracing::warn!(
                        "online verification unreachable, continuing on cached check: {}",
                        e
                    );
                    Ok(())
                } else {
                    Err(e)
                }
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

    pub fn update_key_used_time(&mut self, auth_str: &String) {
        self.key_used_time = format!("key_used_time:{}", md5_hex(auth_str));
    }

    pub async fn verify_appkey(&self, appkey: String) -> bool {
        if appkey.is_empty() {
            return false;
        }
        let app_secret = calculate_app_secret(appkey.clone());
        self.auth.lock().await.appkey == appkey && self.auth.lock().await.app_secret == app_secret
    }

    pub async fn start_count_down() {
        tokio::spawn(async move {
            loop {
                tokio::time::sleep(std::time::Duration::from_secs(5)).await;
                // auth
                let auth = gAuthManager.lock().await.get_auth().await;
                if auth.auth_id.is_empty() {
                    continue;
                }

                let key_used_time = gAuthManager.lock().await.key_used_time.clone();

                // used time: verify existing signed record, increment, and write a new signed record
                let storage = gKvStorage.lock().await;
                let r = storage.get(key_used_time.as_str());
                drop(storage);

                let mut used_time_ms: i64 = 0;
                if let Some(record) = r {
                    match verify_used_time(&record, &auth) {
                        Ok(v) => used_time_ms = v,
                        Err(e) => {
                            tracing::error!(
                                "used-time record verification failed, resetting: {}",
                                e
                            );
                            used_time_ms = 0;
                        }
                    }
                }

                used_time_ms += 5000;
                let signed_record = sign_used_time(used_time_ms, auth.end_timestamp_ms, &auth);
                gKvStorage
                    .lock()
                    .await
                    .put(key_used_time.as_str(), signed_record.as_str());

                // save a plain-number backup for the panel UI
                let file = File::options()
                    .write(true)
                    .create(true)
                    .append(false)
                    .truncate(true)
                    .open("au.dat");
                if let Ok(mut file) = file {
                    let _ = file.write_all(used_time_ms.to_string().as_bytes());
                }
            }
        });
    }

    pub async fn get_used_time(&self) -> i64 {
        let auth = self.get_auth().await;
        if auth.auth_id.is_empty() {
            return 0;
        }
        let record = gKvStorage
            .lock()
            .await
            .get(self.key_used_time.as_str())
            .unwrap_or_default();
        if record.is_empty() {
            return 0;
        }
        match verify_used_time(&record, &auth) {
            Ok(v) => v,
            Err(e) => {
                tracing::error!("used-time verification failed: {}", e);
                0
            }
        }
    }

    pub async fn clear_used_time(&self) {
        let auth = self.get_auth().await;
        if auth.auth_id.is_empty() {
            gKvStorage
                .lock()
                .await
                .put(self.key_used_time.as_str(), "0");
            return;
        }
        let signed_record = sign_used_time(0, auth.end_timestamp_ms, &auth);
        gKvStorage
            .lock()
            .await
            .put(self.key_used_time.as_str(), signed_record.as_str());
    }

    pub async fn is_auth_ok(&self, auth_id: String, auth_password: String) -> bool {
        let self_auth_id = self.auth.lock().await.auth_id.clone();
        let self_auth_password = self.auth.lock().await.password.clone();
        auth_id == self_auth_id && auth_password == self_auth_password
    }
}
