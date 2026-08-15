use serde::{Deserialize, Serialize};

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct OffIssue {
    #[serde(default)]
    pub item_id: String,

    #[serde(default)]
    pub title: String,

    #[serde(default)]
    pub your_name: String,

    #[serde(default)]
    pub desc: String,

    #[serde(default)]
    pub version: String,

    #[serde(default)]
    pub os: String,

    // email
    #[serde(default)]
    pub email: String,

    // wechat
    #[serde(default)]
    pub wechat: String,

    // qq
    #[serde(default)]
    pub qq: String,

    #[serde(default)]
    pub created_ts: i64,

    #[serde(default)]
    pub created_ts_readable: String,

    #[serde(default)]
    pub processed: bool,
}
