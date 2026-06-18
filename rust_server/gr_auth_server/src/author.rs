use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Author {
    pub name: String,
    pub password_hash: String,
    pub permission: String,
}
