use crate::error::ApiError;
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Clone, Copy)]
pub enum Kind {
    Consult,
    Issue,
}
impl Kind {
    pub fn name(self) -> &'static str {
        match self {
            Self::Consult => "consult",
            Self::Issue => "issue",
        }
    }
}

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Submission {
    pub request_id: Uuid,
    pub title: String,
    pub your_name: String,
    pub description: String,
    pub email: String,
    pub wechat: String,
    pub qq: String,
    pub consult_type: Option<String>,
    pub version: Option<String>,
    pub os: Option<String>,
}
fn text(value: &str, min: usize, max: usize) -> bool {
    let length = value.chars().count();
    length >= min
        && length <= max
        && !value.contains('\0')
        && (min == 0 || !value.trim().is_empty())
}
impl Submission {
    pub fn validate(&self, kind: Kind) -> Result<(), ApiError> {
        let common = !self.request_id.is_nil()
            && text(&self.title, 1, 128)
            && text(&self.your_name, 1, 128)
            && text(&self.description, 1, 8192)
            && text(&self.email, 0, 320)
            && text(&self.wechat, 0, 128)
            && text(&self.qq, 0, 64);
        let specific = match kind {
            Kind::Consult => {
                matches!(
                    self.consult_type.as_deref(),
                    Some("personal" | "enterprise")
                ) && self.version.is_none()
                    && self.os.is_none()
            }
            Kind::Issue => {
                self.consult_type.is_none()
                    && self
                        .version
                        .as_deref()
                        .is_some_and(|field_value| text(field_value, 1, 64))
                    && self
                        .os
                        .as_deref()
                        .is_some_and(|field_value| text(field_value, 1, 64))
            }
        };
        if common && specific {
            Ok(())
        } else {
            Err(ApiError::Invalid)
        }
    }
}

#[derive(Serialize, sqlx::FromRow)]
pub struct Feedback {
    pub id: Uuid,
    pub title: String,
    pub your_name: String,
    pub description: String,
    pub email: String,
    pub wechat: String,
    pub qq: String,
    pub consult_type: Option<String>,
    pub version: Option<String>,
    pub os: Option<String>,
    pub processed: bool,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Page {
    pub page: u32,
    pub page_size: u32,
    pub processed: Option<bool>,
}
impl Page {
    pub fn validate(&self) -> Result<(), ApiError> {
        if (1..=10000).contains(&self.page) && (1..=100).contains(&self.page_size) {
            Ok(())
        } else {
            Err(ApiError::Invalid)
        }
    }
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Mark {
    pub expected_revision: i64,
    pub processed: bool,
}

pub use px_release_catalog::{ReleaseQuery, ReleaseSpec as ReleaseInput};

#[derive(Serialize, sqlx::FromRow)]
pub struct Release {
    pub id: Uuid,
    pub product: String,
    pub distribution: String,
    pub channel: String,
    pub os: String,
    pub architecture: String,
    pub build_number: i64,
    pub version: String,
    pub artifact_url: String,
    pub sha256: String,
    pub size_bytes: i64,
    pub metadata_url: String,
    pub metadata_sha256: String,
    pub created_at: DateTime<Utc>,
}
