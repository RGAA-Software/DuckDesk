use std::fs::File;
use std::io::Write;
use std::sync::Arc;
use tokio::sync::Mutex;
use gr_base::md5_hex;
use gr_auth_mgr::app_secret_util::{calculate_app_secret, is_appkey_secret_paired};
use gr_auth_mgr::auth_util::{parse_authorization, verify_authorization};
use gr_auth_mgr::authorization::Authorization;
use crate::{gAuthManager, gKvStorage};

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
        let auth_str = if let Some(str) = gKvStorage
            .lock().await
            .get(KEY_AUTHORIZATION) {
            str
        }
        else {
            let default_auth = std::fs::read_to_string("auth/auth.info")
                .expect("can't read aut/auth.info");
            default_auth
        };

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

        tracing::info!("use auth: {:#?}", auth);

        self.update_key_used_time(&auth_str);
        self.update_auth(auth).await;
        true
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
        self.auth.lock().await.appkey == appkey &&
            self.auth.lock().await.app_secret == app_secret
    }

    pub async fn start_count_down() {
        tokio::spawn(async move {
            loop {
                tokio::time::sleep(std::time::Duration::from_secs(5)).await;
                // auth
                let auth = gAuthManager
                    .lock().await
                    .get_auth().await;
                if auth.auth_name == "Free" {
                    tracing::info!("this is free authorization, ignore it");
                    continue;
                }

                let key_used_time = gAuthManager
                    .lock().await
                    .key_used_time.clone();

                // used time
                let r = gKvStorage
                    .lock().await
                    .get(key_used_time.as_str());
                if let Some(used_time) = r {
                    let used_time = used_time.parse::<i64>();
                    if let Ok(used_time) = used_time {
                        //tracing::info!("the used time: {}", used_time);
                        let used_time = used_time + 5000;
                        gKvStorage
                            .lock().await
                            .put(key_used_time.as_str(), used_time.to_string().as_str());

                        // save to a file
                        let file = File::options()
                            .write(true)
                            .create(true)
                            .append(false)
                            .truncate(true)
                            .open("au.dat");
                        if let Ok(mut file) = file {
                            let _ = file.write_all(used_time.to_string().as_bytes());
                        }
                    }
                    else {
                        gKvStorage
                            .lock().await
                            .put(key_used_time.as_str(), String::from("5000").as_str());
                    }
                }
                else {
                    gKvStorage
                        .lock().await
                        .put(key_used_time.as_str(), String::from("5000").as_str());
                }
            }
        });
    }

    pub async fn get_used_time(&self) -> i64 {
        gKvStorage
            .lock().await
            .get(self.key_used_time.as_str()).unwrap_or("0".to_string())
            .parse::<i64>().unwrap_or(0)
    }

    pub async fn clear_used_time(&self) {
        gKvStorage
            .lock().await
            .put(self.key_used_time.as_str(), String::from("0").as_str());
    }

    pub async fn is_auth_ok(&self, auth_id: String, auth_password: String) -> bool {
        let self_auth_id = self.auth
            .lock().await
            .auth_id.clone();
        let self_auth_password = self.auth
            .lock().await
            .password.clone();
        auth_id == self_auth_id && auth_password == self_auth_password
    }

}