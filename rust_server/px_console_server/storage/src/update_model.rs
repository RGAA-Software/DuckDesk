use crate::StoreError;
use chrono::{DateTime, Utc};
use px_release_catalog::{ReleaseQuery, ReleaseSpec};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UpdateDecision {
    Approve,
    Withdraw,
}
impl UpdateDecision {
    pub(crate) fn state(self) -> &'static str {
        match self {
            Self::Approve => "approved",
            Self::Withdraw => "withdrawn",
        }
    }
}
#[derive(Debug, Clone, Serialize)]
pub struct UpdateRelease {
    pub id: Uuid,
    pub artifact: ReleaseSpec,
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct UpdateRow {
    pub id: Uuid,
    pub request_hash: Vec<u8>,
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
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}
impl UpdateRow {
    pub(crate) fn view(self) -> Result<UpdateRelease, StoreError> {
        let artifact = ReleaseSpec {
            target: ReleaseQuery {
                product: self.product.parse().map_err(|_| StoreError::Rejected)?,
                distribution: self
                    .distribution
                    .parse()
                    .map_err(|_| StoreError::Rejected)?,
                channel: self.channel.parse().map_err(|_| StoreError::Rejected)?,
                os: self.os.parse().map_err(|_| StoreError::Rejected)?,
                architecture: self
                    .architecture
                    .parse()
                    .map_err(|_| StoreError::Rejected)?,
            },
            build_number: self.build_number,
            version: self.version,
            artifact_url: self.artifact_url,
            sha256: self.sha256,
            size_bytes: self.size_bytes,
            metadata_url: self.metadata_url,
            metadata_sha256: self.metadata_sha256,
        };
        artifact.validate().map_err(|_| StoreError::Rejected)?;
        Ok(UpdateRelease {
            id: self.id,
            artifact,
            state: self.state,
            revision: self.revision,
            created_at: self.created_at,
            updated_at: self.updated_at,
        })
    }
}
