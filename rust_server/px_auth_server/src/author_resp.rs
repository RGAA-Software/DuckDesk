use crate::author::AuthorRole;
use serde::Serialize;

#[derive(Serialize, Default)]
pub struct AuthorLoginResp {
    pub token: String,
}

#[derive(Serialize, Default)]
pub struct AuthorMeResp {
    pub name: String,
    pub role: AuthorRole,
}

#[derive(Serialize, Default)]
pub struct AuthorLogOutResp {
    pub message: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::author::AuthorRole;

    #[test]
    fn author_me_response_uses_role_field() {
        let value = serde_json::to_value(AuthorMeResp {
            name: "Admin".to_string(),
            role: AuthorRole::Admin,
        })
        .unwrap();

        assert_eq!(value["name"], "Admin");
        assert_eq!(value["role"], "admin");
        assert!(value.get("permission").is_none());
    }
}
