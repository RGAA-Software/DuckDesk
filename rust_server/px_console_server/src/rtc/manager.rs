use crate::rtc::model::{
    ConsoleRtcSettings, RtcCredentialMode, RtcIceConfig, RtcIceConfigView,
    RtcSessionIceConfig, RtcSessionIceServer,
};
use base64::engine::general_purpose::STANDARD as BASE64_STANDARD;
use base64::Engine;
use ring::hmac;
use ring::rand::{SecureRandom, SystemRandom};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::sync::{Mutex, RwLock};

const CONFIG_FILE_NAME: &str = "rtc_ice_config.json";
const SECRET_FILE_NAME: &str = "turn_rest_secret";

#[derive(Debug, Clone, Serialize, Deserialize)]
struct PersistedRtcIceConfig {
    config: RtcIceConfig,
}

pub struct RtcConfigManager {
    config: RwLock<RtcIceConfig>,
    secret: RwLock<Vec<u8>>,
    storage_dir: Mutex<Option<PathBuf>>,
}

impl RtcConfigManager {
    pub fn new() -> Self {
        Self {
            config: RwLock::new(RtcIceConfig::from_settings(
                ConsoleRtcSettings::default(),
                "127.0.0.1",
            )),
            secret: RwLock::new(Vec::new()),
            storage_dir: Mutex::new(None),
        }
    }

    pub async fn initialize(
        &self,
        settings: ConsoleRtcSettings,
        console_host: &str,
        storage_dir: PathBuf,
    ) -> Result<(), String> {
        std::fs::create_dir_all(&storage_dir)
            .map_err(|error| format!("create RTC storage directory failed: {error}"))?;
        let default_config = RtcIceConfig::from_settings(settings, console_host);
        default_config.validate()?;
        let config_path = storage_dir.join(CONFIG_FILE_NAME);
        let config = if config_path.is_file() {
            match Self::read_persisted_config(&config_path) {
                Ok(config) => config,
                Err(error) => {
                    tracing::error!(%error, "stored RTC ICE configuration is invalid; using Console defaults");
                    default_config
                }
            }
        } else {
            default_config
        };
        let secret = Self::load_or_create_secret(&storage_dir.join(SECRET_FILE_NAME))?;
        *self.config.write().await = config;
        *self.secret.write().await = secret;
        *self.storage_dir.lock().await = Some(storage_dir);
        Ok(())
    }

    pub async fn config(&self) -> RtcIceConfig {
        self.config.read().await.clone()
    }

    pub async fn public_config(&self) -> RtcIceConfigView {
        self.config.read().await.public_view()
    }

    pub async fn update(&self, mut next: RtcIceConfig) -> Result<RtcIceConfig, String> {
        next = self.prepare_update(next).await?;
        self.commit(next.clone()).await?;
        Ok(next)
    }

    pub async fn prepare_update(&self, mut next: RtcIceConfig) -> Result<RtcIceConfig, String> {
        next.normalize()?;
        let current = self.config.read().await.clone();
        if next.revision != current.revision {
            return Err("RTC ICE configuration revision conflict".to_string());
        }
        next.revision = current.revision.saturating_add(1);
        Ok(next)
    }

    pub async fn commit(&self, next: RtcIceConfig) -> Result<(), String> {
        next.validate()?;
        self.persist(&next).await?;
        *self.config.write().await = next;
        Ok(())
    }

    pub async fn turn_rest_secret_base64(&self) -> Result<String, String> {
        let secret = self.secret.read().await;
        if secret.is_empty() {
            return Err("TURN REST secret is not initialized".to_string());
        }
        Ok(BASE64_STANDARD.encode(secret.as_slice()))
    }

    pub async fn storage_dir(&self) -> Result<PathBuf, String> {
        self.storage_dir
            .lock()
            .await
            .clone()
            .ok_or_else(|| "RTC configuration manager is not initialized".to_string())
    }

    pub async fn issue_session_config(&self, subject: &str) -> Result<RtcSessionIceConfig, String> {
        if subject.is_empty()
            || subject.len() > 128
            || subject.chars().any(|ch| ch.is_control() || ch.is_whitespace())
        {
            return Err("RTC credential subject is invalid".to_string());
        }
        let config = self.config.read().await.clone();
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| "system clock is before UNIX epoch".to_string())?
            .as_secs();
        let expires_at_seconds = now
            .saturating_add(config.managed_console_server.credential_ttl_seconds);
        let username = format!("{expires_at_seconds}:{subject}");
        let secret = self.secret.read().await;
        if secret.is_empty() {
            return Err("TURN REST secret is not initialized".to_string());
        }
        // Coturn receives the base64 text as `static-auth-secret`. TURN REST
        // credentials must therefore be signed with those exact text bytes,
        // not with the decoded random bytes kept in memory.
        let coturn_secret = BASE64_STANDARD.encode(secret.as_slice());
        let signature = hmac::sign(
            &hmac::Key::new(
                hmac::HMAC_SHA1_FOR_LEGACY_USE_ONLY,
                coturn_secret.as_bytes(),
            ),
            username.as_bytes(),
        );
        let ephemeral_credential = BASE64_STANDARD.encode(signature.as_ref());

        let view = config.public_view();
        let mut servers = Vec::new();
        for server in view.servers {
            let (server_username, credential) = if server.managed
                || server.credential_mode == RtcCredentialMode::ConsoleEphemeral
            {
                (Some(username.clone()), Some(ephemeral_credential.clone()))
            } else if server.credential_mode == RtcCredentialMode::Static {
                let source = config
                    .additional_servers
                    .iter()
                    .find(|candidate| candidate.id == server.id)
                    .ok_or_else(|| "ICE server disappeared while issuing credentials".to_string())?;
                (Some(source.username.clone()), Some(source.credential.clone()))
            } else {
                (None, None)
            };
            servers.push(RtcSessionIceServer {
                id: server.id,
                urls: server.urls,
                username: server_username,
                credential,
            });
        }
        Ok(RtcSessionIceConfig {
            revision: config.revision,
            direct_probe_enabled: config.direct_probe_enabled,
            expires_at: (expires_at_seconds as i64) * 1000,
            ice_servers: servers,
        })
    }

    async fn persist(&self, config: &RtcIceConfig) -> Result<(), String> {
        let storage_dir = self
            .storage_dir
            .lock()
            .await
            .clone()
            .ok_or_else(|| "RTC configuration manager is not initialized".to_string())?;
        let serialized = serde_json::to_vec_pretty(&PersistedRtcIceConfig {
            config: config.clone(),
        })
        .map_err(|error| format!("serialize RTC ICE configuration failed: {error}"))?;
        std::fs::write(storage_dir.join(CONFIG_FILE_NAME), serialized)
            .map_err(|error| format!("persist RTC ICE configuration failed: {error}"))
    }

    fn read_persisted_config(path: &Path) -> Result<RtcIceConfig, String> {
        let bytes = std::fs::read(path)
            .map_err(|error| format!("read stored RTC ICE configuration failed: {error}"))?;
        let mut persisted: PersistedRtcIceConfig = serde_json::from_slice(&bytes)
            .map_err(|error| format!("parse stored RTC ICE configuration failed: {error}"))?;
        persisted.config.normalize()?;
        Ok(persisted.config)
    }

    fn load_or_create_secret(path: &Path) -> Result<Vec<u8>, String> {
        if path.is_file() {
            let encoded = std::fs::read_to_string(path)
                .map_err(|error| format!("read TURN REST secret failed: {error}"))?;
            let secret = BASE64_STANDARD
                .decode(encoded.trim())
                .map_err(|error| format!("decode TURN REST secret failed: {error}"))?;
            if secret.len() < 32 {
                return Err("stored TURN REST secret is too short".to_string());
            }
            return Ok(secret);
        }
        let mut secret = vec![0_u8; 32];
        SystemRandom::new()
            .fill(&mut secret)
            .map_err(|_| "generate TURN REST secret failed".to_string())?;
        std::fs::write(path, BASE64_STANDARD.encode(&secret))
            .map_err(|error| format!("persist TURN REST secret failed: {error}"))?;
        Ok(secret)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rtc::model::{AdditionalIceServerConfig, RtcCredentialMode};

    fn temp_dir(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "px_console_rtc_{name}_{}_{}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ))
    }

    #[tokio::test]
    async fn persists_revision_and_issues_short_lived_credentials() {
        let dir = temp_dir("persist");
        let manager = RtcConfigManager::new();
        manager
            .initialize(ConsoleRtcSettings::default(), "10.0.0.8", dir.clone())
            .await
            .unwrap();
        let mut config = manager.config().await;
        config.additional_servers.push(AdditionalIceServerConfig {
            id: "external".to_string(),
            name: "External".to_string(),
            enabled: true,
            urls: vec!["turn:external.example:3478".to_string()],
            credential_mode: RtcCredentialMode::Static,
            username: "static-user".to_string(),
            credential: "static-password".to_string(),
        });
        let updated = manager.update(config).await.unwrap();
        assert_eq!(updated.revision, 2);

        let session = manager.issue_session_config("device-1:stream-1").await.unwrap();
        assert_eq!(session.revision, 2);
        assert_eq!(session.ice_servers.len(), 2);
        assert!(session.ice_servers[0].username.as_ref().unwrap().contains(':'));
        let username = session.ice_servers[0].username.as_deref().unwrap();
        let coturn_secret = manager.turn_rest_secret_base64().await.unwrap();
        let expected = BASE64_STANDARD.encode(
            hmac::sign(
                &hmac::Key::new(
                    hmac::HMAC_SHA1_FOR_LEGACY_USE_ONLY,
                    coturn_secret.as_bytes(),
                ),
                username.as_bytes(),
            )
            .as_ref(),
        );
        assert_eq!(
            session.ice_servers[0].credential.as_deref(),
            Some(expected.as_str())
        );
        assert_eq!(
            session.ice_servers[1].credential.as_deref(),
            Some("static-password")
        );

        let reloaded = RtcConfigManager::new();
        reloaded
            .initialize(ConsoleRtcSettings::default(), "127.0.0.1", dir.clone())
            .await
            .unwrap();
        assert_eq!(reloaded.config().await.revision, 2);
        assert_eq!(reloaded.public_config().await.servers.len(), 2);
        let _ = std::fs::remove_dir_all(dir);
    }

    #[tokio::test]
    async fn rejects_stale_revision() {
        let dir = temp_dir("revision");
        let manager = RtcConfigManager::new();
        manager
            .initialize(ConsoleRtcSettings::default(), "127.0.0.1", dir.clone())
            .await
            .unwrap();
        let mut config = manager.config().await;
        config.revision = 0;
        assert!(manager.update(config).await.is_err());
        let _ = std::fs::remove_dir_all(dir);
    }
}
