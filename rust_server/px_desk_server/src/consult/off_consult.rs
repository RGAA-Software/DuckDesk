use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct OffConsult {
    pub item_id: String,
    //
    pub title: String,

    pub your_name: String,

    // personal/company
    pub consult_type: String,

    //
    pub content: String,

    // email
    pub email: String,

    // wechat
    pub wechat: String,

    // qq
    pub qq: String,

    // created timestamp
    pub created_ts_readable: String,

    pub created_ts: i64,

    pub processed: bool,

    pub updated_ts_readable: String,

    pub updated_ts: i64,
}
