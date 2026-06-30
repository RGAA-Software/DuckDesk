use crate::gAuthorSettings;
use rand::distr::{Alphanumeric, SampleString};
use serde::Deserialize;

const MIN_JWT_SECRET_LEN: usize = 32;
const JWT_SECRET_LEN: usize = 48;

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
    InvalidField {
        field: &'static str,
        message: &'static str,
    },
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
        let toml_content =
            std::fs::read_to_string(path).map_err(|e| AuthorSettingsError::ReadFile {
                path: path.to_string(),
                message: e.to_string(),
            })?;

        // Auto-generate jwt_secret on every startup and write back to the file.
        let new_secret = generate_random_secret();
        let updated_content = replace_toml_string_field(&toml_content, "jwt_secret", &new_secret)
            .ok_or_else(|| AuthorSettingsError::InvalidField {
                field: "bootstrap.jwt_secret",
                message: "field not found in settings file",
            })?;

        // Parse the updated content first; only write back if parsing succeeds.
        let settings = Self::parse_from_toml(&updated_content)?;

        std::fs::write(path, &updated_content).map_err(|e| AuthorSettingsError::ReadFile {
            path: path.to_string(),
            message: e.to_string(),
        })?;
        tracing::info!("auto-generated jwt_secret and wrote back to {}", path);

        Ok(settings)
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
        validate_optional_secret(
            "bootstrap.admin_password",
            self.bootstrap.admin_password.as_deref(),
        )?;
        validate_non_empty("bootstrap.visitor_name", &self.bootstrap.visitor_name)?;
        validate_optional_secret(
            "bootstrap.visitor_password",
            self.bootstrap.visitor_password.as_deref(),
        )?;
        Ok(())
    }
}

/// Generates a random alphanumeric secret suitable for JWT signing.
fn generate_random_secret() -> String {
    Alphanumeric.sample_string(&mut rand::rng(), JWT_SECRET_LEN)
}

/// Replaces a top-level or table-level string field in a TOML document,
/// preserving all comments, whitespace, and line endings.
/// Only the value of `field` is changed; every other line is kept verbatim.
/// Handles mixed LF/CRLF line endings correctly.
fn replace_toml_string_field(content: &str, field: &str, new_value: &str) -> Option<String> {
    let mut found = false;

    // Always split by '\n'; strip trailing '\r' per-line for CRLF/mixed files.
    // This handles pure LF, pure CRLF, and mixed line endings.
    let lines: Vec<String> = content
        .split('\n')
        .map(|line| {
            // Strip trailing '\r' (from CRLF) for key matching, but preserve it in output.
            let (body, cr) = line.strip_suffix('\r')
                .map(|b| (b, "\r"))
                .unwrap_or((line, ""));

            let Some(eq_pos) = body.find('=') else {
                return line.to_string();
            };
            let key = body[..eq_pos].trim();
            if key != field {
                return line.to_string();
            }
            found = true;
            let indent_len = body.len() - body.trim_start().len();
            let indent = &body[..indent_len];
            format!("{}{} = \"{}\"{}", indent, field, new_value, cr)
        })
        .collect();

    if !found {
        return None;
    }

    // Rejoin with '\n'; each line already carries its own trailing '\r' if it had one.
    Some(lines.join("\n"))
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

fn validate_optional_secret(
    field: &'static str,
    value: Option<&str>,
) -> Result<(), AuthorSettingsError> {
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
            admin_name: "".to_string(),
            admin_password: None,
            visitor_name: "".to_string(),
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
"#
        .to_string()
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
        let err = AuthorSettings::parse_from_toml(
            r#"
server_port = 30400
verify_server = "https://godesk.uk:30400"

[bootstrap]
jwt_secret = "test-secret-must-be-at-least-32-bytes"
admin_name = "Admin"
visitor_name = "Visitor"
"#,
        )
        .unwrap_err();

        assert!(matches!(err, AuthorSettingsError::ParseToml(_)));
    }

    #[test]
    fn rejects_empty_required_fields() {
        let cases = [
            (
                "db_path",
                r#"db_path = "mongodb://localhost:27017/""#,
                r#"db_path = " ""#,
            ),
            (
                "verify_server",
                r#"verify_server = "https://godesk.uk:30400""#,
                r#"verify_server = " ""#,
            ),
            (
                "bootstrap.admin_name",
                r#"admin_name = "Admin""#,
                r#"admin_name = " ""#,
            ),
            (
                "bootstrap.visitor_name",
                r#"visitor_name = "Visitor""#,
                r#"visitor_name = " ""#,
            ),
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
        let toml = valid_settings_toml().replace("server_port = 30400", "server_port = 0");
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
            (
                "CHANGE_ME_TO_A_RANDOM_SECRET_AT_LEAST_32_CHARS",
                "must not be a placeholder",
            ),
            (
                "<random secret with at least 32 characters>",
                "must not be a placeholder",
            ),
        ];

        for (secret, message) in cases {
            let toml =
                valid_settings_toml().replace("test-secret-must-be-at-least-32-bytes", secret);
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
        let admin_toml = valid_settings_toml().replace(
            r#"admin_password = "admin-password""#,
            r#"admin_password = "CHANGE_ME_ADMIN_PASSWORD""#,
        );
        let err = AuthorSettings::parse_from_toml(&admin_toml).unwrap_err();
        assert_eq!(
            err,
            AuthorSettingsError::InvalidField {
                field: "bootstrap.admin_password",
                message: "must not be a placeholder",
            }
        );

        let visitor_toml = valid_settings_toml().replace(
            r#"visitor_password = """#,
            r#"visitor_password = "<visitor password>""#,
        );
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
            .replace(
                r#"admin_password = "admin-password"
"#,
                "",
            )
            .replace(
                r#"visitor_password = ""
"#,
                "",
            );
        assert!(AuthorSettings::parse_from_toml(&without_passwords).is_ok());

        let empty_passwords = valid_settings_toml().replace(
            r#"admin_password = "admin-password""#,
            r#"admin_password = """#,
        );
        assert!(AuthorSettings::parse_from_toml(&empty_passwords).is_ok());
    }

    #[test]
    fn reports_read_file_errors_without_panic() {
        let err =
            AuthorSettings::load_settings_from_file("missing-gr-auth-settings.toml").unwrap_err();

        assert!(matches!(err, AuthorSettingsError::ReadFile { .. }));
    }

    #[test]
    fn generate_random_secret_meets_length_requirement() {
        let secret = generate_random_secret();
        assert!(secret.len() >= MIN_JWT_SECRET_LEN);
        assert!(secret.chars().all(|c| c.is_ascii_alphanumeric()));
    }

    #[test]
    fn generate_random_secret_is_unique() {
        let a = generate_random_secret();
        let b = generate_random_secret();
        assert_ne!(a, b);
    }

    #[test]
    fn replace_toml_string_field_replaces_value() {
        let toml = r#"
server_port = 30400

[bootstrap]
# comment
jwt_secret = "old-value"
admin_name = "Admin"
"#;
        let result = replace_toml_string_field(toml, "jwt_secret", "new-value").unwrap();
        assert!(result.contains("jwt_secret = \"new-value\""));
        assert!(!result.contains("old-value"));
    }

    #[test]
    fn replace_toml_string_field_preserves_comments() {
        let toml = r#"
[bootstrap]
# Required on every startup.
# Changing it invalidates existing login tokens.
jwt_secret = "old-value"
admin_name = "Admin"
"#;
        let result = replace_toml_string_field(toml, "jwt_secret", "new-value").unwrap();
        assert!(result.contains("# Required on every startup."));
        assert!(result.contains("# Changing it invalidates existing login tokens."));
        assert!(result.contains("admin_name = \"Admin\""));
    }

    #[test]
    fn replace_toml_string_field_preserves_trailing_newline() {
        let toml = "[bootstrap]\njwt_secret = \"old\"\n";
        let result = replace_toml_string_field(toml, "jwt_secret", "new").unwrap();
        assert!(result.ends_with('\n'));
    }

    #[test]
    fn replace_toml_string_field_preserves_crlf() {
        let toml = "[bootstrap]\r\njwt_secret = \"old\"\r\n";
        let result = replace_toml_string_field(toml, "jwt_secret", "new").unwrap();
        assert!(result.contains("\r\n"));
        assert!(result.ends_with("\r\n"));
    }

    #[test]
    fn replace_toml_string_field_preserves_indentation() {
        let toml = "[bootstrap]\n  jwt_secret = \"old\"\n";
        let result = replace_toml_string_field(toml, "jwt_secret", "new").unwrap();
        assert!(result.contains("  jwt_secret = \"new\""));
    }

    #[test]
    fn replace_toml_string_field_returns_none_for_missing_field() {
        let toml = "[bootstrap]\nadmin_name = \"Admin\"\n";
        assert!(replace_toml_string_field(toml, "jwt_secret", "new").is_none());
    }

    #[test]
    fn replace_toml_string_field_handles_mixed_line_endings() {
        // Simulates a file where most lines use LF but one line uses CRLF.
        let toml = "server_port = 30400\n\
db_path = \"mongodb://localhost:27017/\"\n\
verify_server = \"https://localhost:30400\"\n\
\n\
[bootstrap]\n\
jwt_secret = \"old-value\"\n\
admin_name = \"Admin\"\r\n\
admin_password = \"secret\"\n";
        let result = replace_toml_string_field(toml, "jwt_secret", "new-value").unwrap();
        assert!(result.contains("jwt_secret = \"new-value\""));
        assert!(!result.contains("old-value"));
        // CRLF on admin_name line preserved
        assert!(result.contains("admin_name = \"Admin\"\r\n"));
        // LF on other lines preserved
        assert!(result.contains("server_port = 30400\n"));
    }

    #[test]
    fn load_settings_from_file_auto_generates_and_writes_back() {
        let dir = std::env::temp_dir().join("gr_auth_settings_auto_jwt_test");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("gr_auth_server_settings.toml");
        std::fs::write(
            &path,
            r#"server_port = 30400
db_path = "mongodb://localhost:27017/"
verify_server = "https://godesk.uk:30400"

[bootstrap]
# comment that must be preserved
jwt_secret = "CHANGE_ME_TO_A_RANDOM_SECRET_AT_LEAST_32_CHARS"
admin_name = "Admin"
admin_password = ""
visitor_name = "Visitor"
visitor_password = ""
"#,
        )
        .unwrap();

        let settings = AuthorSettings::load_settings_from_file(path.to_str().unwrap()).unwrap();
        assert!(settings.bootstrap.jwt_secret.len() >= MIN_JWT_SECRET_LEN);
        assert_ne!(
            settings.bootstrap.jwt_secret,
            "CHANGE_ME_TO_A_RANDOM_SECRET_AT_LEAST_32_CHARS"
        );

        let written = std::fs::read_to_string(&path).unwrap();
        assert!(written.contains("# comment that must be preserved"));
        assert!(written.contains(&settings.bootstrap.jwt_secret));
        assert!(!written.contains("CHANGE_ME"));

        std::fs::remove_dir_all(&dir).ok();
    }
}
