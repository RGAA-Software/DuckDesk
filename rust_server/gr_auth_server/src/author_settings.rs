use serde::Deserialize;
use crate::gAuthorSettings;

const MIN_JWT_SECRET_LEN: usize = 32;

#[derive(Debug, Deserialize)]
pub struct AuthorSettings {
    pub server_port: u16,
    pub db_path: String,
    // to verify authorization
    pub verify_server: String,
    pub bootstrap: BootstrapSettings,
}

#[derive(Debug, Deserialize)]
pub struct BootstrapSettings {
    pub jwt_secret: String,
    pub admin_name: String,
    pub admin_password: Option<String>,
    pub visitor_name: String,
    pub visitor_password: Option<String>,
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum AuthorSettingsError {
    #[error("failed to read settings file '{path}': {message}")]
    ReadFile { path: String, message: String },
    #[error("failed to parse settings toml: {0}")]
    ParseToml(String),
    #[error("invalid settings field '{field}': {message}")]
    InvalidField { field: &'static str, message: &'static str },
}

impl AuthorSettings {
    pub fn new() -> Self {
        AuthorSettings::default()
    }

    pub async fn load_settings() -> bool {
        match Self::load_settings_from_file("gr_auth_server_settings.toml") {
            Ok(st) => {
                tracing::info!(
                    "gr_auth_server settings loaded, server_port={}, db_path={}, verify_server={}, bootstrap.admin_name={}, bootstrap.visitor_name={}",
                    st.server_port,
                    st.db_path,
                    st.verify_server,
                    st.bootstrap.admin_name,
                    st.bootstrap.visitor_name,
                );
                let mut guard = gAuthorSettings.lock().await;
                *guard = st;
                true
            }
            Err(e) => {
                tracing::error!("could not load gr_auth_server settings: {}", e);
                false
            }
        }
    }

    pub fn load_settings_from_file(path: &str) -> Result<Self, AuthorSettingsError> {
        let toml_content = std::fs::read_to_string(path)
            .map_err(|e| AuthorSettingsError::ReadFile {
                path: path.to_string(),
                message: e.to_string(),
            })?;
        Self::parse_from_toml(&toml_content)
    }

    pub fn parse_from_toml(toml_content: &str) -> Result<Self, AuthorSettingsError> {
        let st: AuthorSettings = toml::from_str(toml_content)
            .map_err(|e| AuthorSettingsError::ParseToml(e.to_string()))?;
        st.validate()?;
        Ok(st)
    }

    pub fn validate(&self) -> Result<(), AuthorSettingsError> {
        if self.server_port == 0 {
            return Err(AuthorSettingsError::InvalidField {
                field: "server_port",
                message: "must be greater than 0",
            });
        }
        validate_non_empty("db_path", &self.db_path)?;
        validate_non_empty("verify_server", &self.verify_server)?;
        validate_non_empty("bootstrap.jwt_secret", &self.bootstrap.jwt_secret)?;
        validate_secret("bootstrap.jwt_secret", &self.bootstrap.jwt_secret)?;
        validate_non_empty("bootstrap.admin_name", &self.bootstrap.admin_name)?;
        validate_optional_secret("bootstrap.admin_password", self.bootstrap.admin_password.as_deref())?;
        validate_non_empty("bootstrap.visitor_name", &self.bootstrap.visitor_name)?;
        validate_optional_secret("bootstrap.visitor_password", self.bootstrap.visitor_password.as_deref())?;
        Ok(())
    }

}

fn validate_non_empty(field: &'static str, value: &str) -> Result<(), AuthorSettingsError> {
    if value.trim().is_empty() {
        return Err(AuthorSettingsError::InvalidField {
            field,
            message: "must not be empty",
        });
    }
    Ok(())
}

fn validate_secret(field: &'static str, value: &str) -> Result<(), AuthorSettingsError> {
    let value = value.trim();
    if value.len() < MIN_JWT_SECRET_LEN {
        return Err(AuthorSettingsError::InvalidField {
            field,
            message: "must be at least 32 characters",
        });
    }
    if value.starts_with('<') || value.starts_with("CHANGE_ME") {
        return Err(AuthorSettingsError::InvalidField {
            field,
            message: "must not be a placeholder",
        });
    }
    Ok(())
}

fn validate_optional_secret(field: &'static str, value: Option<&str>) -> Result<(), AuthorSettingsError> {
    let Some(value) = value else {
        return Ok(());
    };
    let value = value.trim();
    if value.is_empty() {
        return Ok(());
    }
    if value.starts_with('<') || value.starts_with("CHANGE_ME") {
        return Err(AuthorSettingsError::InvalidField {
            field,
            message: "must not be a placeholder",
        });
    }
    Ok(())
}

impl Default for AuthorSettings {
    fn default() -> Self {
        AuthorSettings {
            server_port: 0,
            db_path: "".to_string(),
            verify_server: "".to_string(),
            bootstrap: BootstrapSettings::default(),
        }
    }
}

impl Default for BootstrapSettings {
    fn default() -> Self {
        BootstrapSettings {
            jwt_secret: "".to_string(),
            admin_name: "Admin".to_string(),
            admin_password: None,
            visitor_name: "Visitor".to_string(),
            visitor_password: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid_settings_toml() -> String {
        r#"
server_port = 30400
db_path = "mongodb://localhost:27017/"
verify_server = "https://godesk.uk:30400"

[bootstrap]
jwt_secret = "test-secret-must-be-at-least-32-bytes"
admin_name = "Admin"
admin_password = "admin-password"
visitor_name = "Visitor"
visitor_password = ""
"#.to_string()
    }

    #[test]
    fn parses_valid_settings() {
        let settings = AuthorSettings::parse_from_toml(&valid_settings_toml()).unwrap();

        assert_eq!(settings.server_port, 30400);
        assert_eq!(settings.db_path, "mongodb://localhost:27017/");
        assert_eq!(settings.bootstrap.admin_name, "Admin");
        assert_eq!(settings.bootstrap.visitor_password.as_deref(), Some(""));
    }

    #[test]
    fn rejects_missing_required_fields() {
        let err = AuthorSettings::parse_from_toml(r#"
server_port = 30400
verify_server = "https://godesk.uk:30400"

[bootstrap]
jwt_secret = "test-secret-must-be-at-least-32-bytes"
admin_name = "Admin"
visitor_name = "Visitor"
"#).unwrap_err();

        assert!(matches!(err, AuthorSettingsError::ParseToml(_)));
    }

    #[test]
    fn rejects_empty_required_fields() {
        let cases = [
            ("db_path", r#"db_path = "mongodb://localhost:27017/""#, r#"db_path = " ""#),
            ("verify_server", r#"verify_server = "https://godesk.uk:30400""#, r#"verify_server = " ""#),
            ("bootstrap.admin_name", r#"admin_name = "Admin""#, r#"admin_name = " ""#),
            ("bootstrap.visitor_name", r#"visitor_name = "Visitor""#, r#"visitor_name = " ""#),
        ];

        for (field, from, to) in cases {
            let toml = valid_settings_toml().replace(from, to);
            let err = AuthorSettings::parse_from_toml(&toml).unwrap_err();
            assert_eq!(
                err,
                AuthorSettingsError::InvalidField {
                    field,
                    message: "must not be empty",
                }
            );
        }
    }

    #[test]
    fn rejects_zero_server_port() {
        let toml = valid_settings_toml()
            .replace("server_port = 30400", "server_port = 0");
        let err = AuthorSettings::parse_from_toml(&toml).unwrap_err();

        assert_eq!(
            err,
            AuthorSettingsError::InvalidField {
                field: "server_port",
                message: "must be greater than 0",
            }
        );
    }

    #[test]
    fn rejects_invalid_jwt_secret_values() {
        let cases = [
            ("too-short", "must be at least 32 characters"),
            ("CHANGE_ME_TO_A_RANDOM_SECRET_AT_LEAST_32_CHARS", "must not be a placeholder"),
            ("<random secret with at least 32 characters>", "must not be a placeholder"),
        ];

        for (secret, message) in cases {
            let toml = valid_settings_toml()
                .replace("test-secret-must-be-at-least-32-bytes", secret);
            let err = AuthorSettings::parse_from_toml(&toml).unwrap_err();
            assert_eq!(
                err,
                AuthorSettingsError::InvalidField {
                    field: "bootstrap.jwt_secret",
                    message,
                }
            );
        }
    }

    #[test]
    fn rejects_placeholder_bootstrap_passwords_when_present() {
        let admin_toml = valid_settings_toml()
            .replace(r#"admin_password = "admin-password""#, r#"admin_password = "CHANGE_ME_ADMIN_PASSWORD""#);
        let err = AuthorSettings::parse_from_toml(&admin_toml).unwrap_err();
        assert_eq!(
            err,
            AuthorSettingsError::InvalidField {
                field: "bootstrap.admin_password",
                message: "must not be a placeholder",
            }
        );

        let visitor_toml = valid_settings_toml()
            .replace(r#"visitor_password = """#, r#"visitor_password = "<visitor password>""#);
        let err = AuthorSettings::parse_from_toml(&visitor_toml).unwrap_err();
        assert_eq!(
            err,
            AuthorSettingsError::InvalidField {
                field: "bootstrap.visitor_password",
                message: "must not be a placeholder",
            }
        );
    }

    #[test]
    fn allows_missing_or_empty_optional_bootstrap_passwords() {
        let without_passwords = valid_settings_toml()
            .replace(r#"admin_password = "admin-password"
"#, "")
            .replace(r#"visitor_password = ""
"#, "");
        assert!(AuthorSettings::parse_from_toml(&without_passwords).is_ok());

        let empty_passwords = valid_settings_toml()
            .replace(r#"admin_password = "admin-password""#, r#"admin_password = """#);
        assert!(AuthorSettings::parse_from_toml(&empty_passwords).is_ok());
    }

    #[test]
    fn reports_read_file_errors_without_panic() {
        let err = AuthorSettings::load_settings_from_file("missing-gr-auth-settings.toml")
            .unwrap_err();

        assert!(matches!(err, AuthorSettingsError::ReadFile { .. }));
    }
}
