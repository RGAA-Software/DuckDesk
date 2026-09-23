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
    pub repository_publication_sha256: String,
    pub repository_root_version: i64,
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
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub channel: String,
    pub os: String,
    pub architecture: String,
    pub build_number: i64,
    pub version: String,
    pub metadata_base_url: String,
    pub targets_base_url: String,
    pub target_name: String,
    pub sha256: String,
    pub repository_publication_sha256: String,
    pub repository_root_version: i64,
    pub platform_signer_sha256: Option<String>,
    pub size_bytes: i64,
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
                release_namespace: self.release_namespace,
                oem_id: self.oem_id,
                channel: self.channel.parse().map_err(|_| StoreError::Rejected)?,
                os: self.os.parse().map_err(|_| StoreError::Rejected)?,
                architecture: self
                    .architecture
                    .parse()
                    .map_err(|_| StoreError::Rejected)?,
            },
            build_number: self.build_number,
            version: self.version,
            metadata_base_url: self.metadata_base_url,
            targets_base_url: self.targets_base_url,
            target_name: self.target_name,
            sha256: self.sha256,
            platform_signer_sha256: self.platform_signer_sha256,
            size_bytes: self.size_bytes,
        };
        artifact
            .validate_immutable_target_name()
            .map_err(|_| StoreError::Rejected)?;
        Ok(UpdateRelease {
            id: self.id,
            artifact,
            repository_publication_sha256: self.repository_publication_sha256,
            repository_root_version: self.repository_root_version,
            state: self.state,
            revision: self.revision,
            created_at: self.created_at,
            updated_at: self.updated_at,
        })
    }
}
