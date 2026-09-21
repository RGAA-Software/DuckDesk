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
    pub metadata_base_url: String,
    pub targets_base_url: String,
    pub target_name: String,
    pub sha256: String,
    pub platform_signer_sha256: Option<String>,
    pub size_bytes: i64,
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct NodeUpdateActivation {
    pub task_id: Uuid,
    pub lease_id: Uuid,
    pub lease_until: DateTime<Utc>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UpdateActivationOutcome {
    Installed,
    Failed { error_code: String },
}

impl UpdateActivationOutcome {
    pub(crate) fn fields(&self) -> (&'static str, Option<&str>) {
        match self {
            Self::Installed => ("installed", None),
            Self::Failed { error_code } => ("failed", Some(error_code)),
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct NodeUpdateCompletion {
    pub state: String,
    pub revision: i64,
    pub error_code: Option<String>,
}

#[derive(sqlx::FromRow)]
pub(crate) struct NodeUpdateActivationRow {
    pub id: Uuid,
    pub release_id: Uuid,
    pub from_build_number: i64,
    pub to_build_number: i64,
    pub lease_id: Uuid,
    pub lease_until: DateTime<Utc>,
}

#[derive(sqlx::FromRow)]
pub(crate) struct NodeUpdateTaskRow {
    pub id: Uuid,
    pub to_build_number: i64,
    pub state: String,
    pub lease_id: Uuid,
    pub revision: i64,
    pub error_code: Option<String>,
}

impl NodeUpdateActivationRow {
    pub(crate) fn grant(self) -> NodeUpdateActivation {
        NodeUpdateActivation {
            task_id: self.id,
            lease_id: self.lease_id,
            lease_until: self.lease_until,
        }
    }
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
            metadata_base_url: self.metadata_base_url,
            targets_base_url: self.targets_base_url,
            target_name: self.target_name,
            sha256: self.sha256,
            platform_signer_sha256: self.platform_signer_sha256,
            size_bytes: self.size_bytes,
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
