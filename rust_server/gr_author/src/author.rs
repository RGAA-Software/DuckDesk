use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Author {
    pub name: String,
    pub password: String,
    pub permission: String,
}