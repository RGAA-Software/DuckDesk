use serde::Deserialize;
use crate::gAuthorSettings;

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

impl AuthorSettings {
    pub fn new() -> Self {
        AuthorSettings::default()
    }

    pub async fn load_settings() {
        let toml_content = std::fs::read_to_string("gr_auth_server_settings.toml")
            .expect("can't read gr_auth_server_settings.toml");
        let st: AuthorSettings = toml::from_str(&toml_content).expect("parse toml failed");
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
    }

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
