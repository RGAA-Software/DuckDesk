use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq, Eq, Default)]
#[serde(rename_all = "snake_case")]
pub enum AuthorRole {
    Admin,
    #[default]
    Visitor,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Author {
    pub name: String,
    pub password_hash: String,
    pub role: AuthorRole,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn author_role_serializes_to_stable_snake_case_values() {
        assert_eq!(serde_json::to_string(&AuthorRole::Admin).unwrap(), "\"admin\"");
        assert_eq!(serde_json::to_string(&AuthorRole::Visitor).unwrap(), "\"visitor\"");
    }

    #[test]
    fn author_role_deserializes_from_stable_snake_case_values() {
        assert_eq!(
            serde_json::from_str::<AuthorRole>("\"admin\"").unwrap(),
            AuthorRole::Admin,
        );
        assert_eq!(
            serde_json::from_str::<AuthorRole>("\"visitor\"").unwrap(),
            AuthorRole::Visitor,
        );
        assert!(serde_json::from_str::<AuthorRole>("\"perm_all\"").is_err());
    }
}
