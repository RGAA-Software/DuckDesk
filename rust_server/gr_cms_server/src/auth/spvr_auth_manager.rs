use crate::auth::spvr_auth_license_keys::parse_and_verify_signed_license;
use crate::{gAuthManager, gKvStorage, gLicenseVerifier, gSpvrContext};
use gr_auth_mgr::app_secret_util::calculate_app_secret;
use gr_auth_mgr::auth_used_time::{sign_used_time, verify_used_time};
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

    /// Loads the authorization from KvStorage (cached deploy string) or
    /// `auth/auth.info` (on-disk). Only signed license format is accepted.
    /// Returns `false` if no valid authorization is found; the server still
    /// starts so the user can upload a license via the web UI.
    pub async fn load(&mut self) -> bool {
        let auth_str = if let Some(str) = gKvStorage.lock().await.get(KEY_AUTHORIZATION) {
            tracing::info!("load: found cached authorization in KvStorage (len={})", str.len());
            str
        } else {
            match std::fs::read_to_string("auth/auth.info") {
                Ok(s) => {
                    tracing::info!("load: found auth/auth.info file (len={})", s.len());
                    s
                }
                Err(e) => {
                    tracing::info!("load: no auth/auth.info found ({}); starting unlicensed", e);
                    return false;
                }
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
                self.update_key_used_time(&auth_str);
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
