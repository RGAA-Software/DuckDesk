use serde::{Deserialize, Serialize};

/// 服务本地配置（exe 旁 desk_settings.toml）。
/// 只存在于部署机，不进入版本库；首次启动自动生成随机管理密码。
#[derive(Serialize, Deserialize, Debug, Clone, Default)]
pub struct OffSettings {
    pub admin: Option<AdminSettings>,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
pub struct AdminSettings {
    pub password: Option<String>,
}

impl OffSettings {
    pub fn settings_path() -> std::path::PathBuf {
        std::env::current_exe()
            .unwrap()
            .parent()
            .unwrap()
            .join("desk_settings.toml")
    }

    /// 加载配置；文件缺失或缺少管理密码时生成随机密码并落盘
    pub fn load_or_create() -> Self {
        let path = Self::settings_path();
        let mut settings: Self = std::fs::read_to_string(&path)
            .ok()
            .and_then(|s| toml::from_str(&s).ok())
            .unwrap_or_default();

        let has_password = settings
            .admin
            .as_ref()
            .and_then(|a| a.password.as_ref())
            .map(|p| !p.is_empty())
            .unwrap_or(false);

        if !has_password {
            let password = generate_password();
            settings.admin = Some(AdminSettings {
                password: Some(password),
            });
            match toml::to_string_pretty(&settings) {
                Ok(content) => {
                    if let Err(e) = std::fs::write(&path, content) {
                        tracing::error!("failed to write settings {:?}: {}", &path, e);
                    } else {
                        tracing::info!("generated new admin password into {:?}", &path);
                    }
                }
                Err(e) => {
                    tracing::error!("failed to serialize settings: {}", e);
                }
            }
        }
        settings
    }

    pub fn admin_password(&self) -> String {
        self.admin
            .as_ref()
            .and_then(|a| a.password.clone())
            .unwrap_or_default()
    }
}

/// 用两段 ObjectId 随机性拼出 48 位十六进制强密码
fn generate_password() -> String {
    format!(
        "{}{}",
        mongodb::bson::oid::ObjectId::new(),
        mongodb::bson::oid::ObjectId::new()
    )
}
