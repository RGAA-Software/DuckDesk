use serde::Deserialize;
use crate::gAuthorSettings;

#[derive(Debug, Deserialize)]
pub struct AuthorSettings {
    pub server_port: u16,
    pub db_path: String,
    // to verify authorization
    pub verify_server: String,
}

impl AuthorSettings {
    pub fn new() -> Self {
        AuthorSettings::default()
    }

    pub async fn load_settings() {
        let toml_content = std::fs::read_to_string("gr_author_settings.toml")
            .expect("can't read gr_author_settings.toml");
        let st: AuthorSettings = toml::from_str(&toml_content).expect("parse toml failed");
        tracing::info!("{:#?}", st);
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
        }
    }
}