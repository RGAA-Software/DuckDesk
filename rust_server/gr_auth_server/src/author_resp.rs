use serde::Serialize;

#[derive(Serialize, Default)]
pub struct AuthorLoginResp {
    pub token: String,
}

#[derive(Serialize, Default)]
pub struct AuthorLogOutResp {
    pub message: String,
}
