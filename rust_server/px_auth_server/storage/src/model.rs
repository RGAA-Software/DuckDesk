use crate::AuthError;
use px_license::{LicensePayload, LicensedService};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LicenseTerms {
    pub customer_id: Uuid,
    pub deployment_id: Uuid,
    pub expires_at: i64,
    pub max_streams: u32,
    pub services: Vec<LicensedService>,
}
impl LicenseTerms {
    pub(crate) fn payload(
        &self,
        id: Uuid,
        revision: i64,
        now: i64,
        key_id: String,
    ) -> Result<LicensePayload, AuthError> {
        if self.customer_id.is_nil() {
            return Err(AuthError::Invalid);
        }
        let payload = LicensePayload {
            schema: 2,
            license_id: id,
            deployment_id: self.deployment_id,
            revision,
            issued_at: now,
            expires_at: self.expires_at,
            max_streams: self.max_streams,
            services: self.services.clone(),
            key_id,
        };
        payload.validate().map_err(|_| AuthError::Invalid)?;
        Ok(payload)
    }
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(tag = "operation", rename_all = "snake_case", deny_unknown_fields)]
pub enum IssueRequest {
    Create {
        terms: LicenseTerms,
    },
    Renew {
        license_id: Uuid,
        expected_revision: i64,
        terms: LicenseTerms,
    },
}
#[derive(Debug, Clone, Serialize, sqlx::FromRow)]
pub struct IssuedLicense {
    pub license_id: Uuid,
    pub revision: i64,
    pub wire: String,
}
#[derive(Debug, Clone, Serialize, sqlx::FromRow)]
pub struct Customer {
    pub id: Uuid,
    pub name: String,
    pub remark: String,
}

pub(crate) fn enum_text<T: Serialize>(value: &T) -> Result<String, AuthError> {
    serde_json::to_value(value)
        .map_err(|_| AuthError::Invalid)?
        .as_str()
        .map(str::to_owned)
        .ok_or(AuthError::Invalid)
}
