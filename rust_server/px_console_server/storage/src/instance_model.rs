use crate::{ClientType, StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;

/// Supplied by the HTTP/WS authentication adapter, never deserialized from an owner ID.
/// The repository checks the selected identity kind; it never tries the other token table.
pub enum ResourceCredential<'a> {
    User(&'a TokenDigest),
    Guest(&'a TokenDigest),
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ResourceOwner {
    User { user_id: Uuid },
    Guest { guest_id: Uuid },
}
impl ResourceOwner {
    pub(crate) fn columns(self) -> (Option<Uuid>, Option<Uuid>) {
        match self {
            Self::User { user_id } => (Some(user_id), None),
            Self::Guest { guest_id } => (None, Some(guest_id)),
        }
    }
}
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct StartApplication {
    pub request_id: Uuid,
    pub application_id: Uuid,
    /// A hard constraint, not a bypass of readiness/authorization/capacity.
    pub deployment_id: Option<Uuid>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PlacementPreviewRequest {
    pub application_id: Uuid,
    pub deployment_id: Option<Uuid>,
}

impl PlacementPreviewRequest {
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if self.application_id.is_nil() || self.deployment_id.is_some_and(|id| id.is_nil()) {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PlacementRejectionReason {
    ApplicationDisabled,
    ApplicationDeleted,
    DeploymentDisabled,
    NodeDisabled,
    NodeDraining,
    NodeDeleted,
    DeviceDisabled,
    DeviceDeleted,
    NodeNotReady,
    NodeDisconnected,
    NodeStale,
    ControlEpochMismatch,
    PublicEndpointMissing,
    InventoryMissing,
    DeploymentNotReady,
    DeploymentGenerationMismatch,
    DeploymentEpochMismatch,
    DeploymentEndpointRevisionMismatch,
    DeploymentRevisionMismatch,
    CapabilityMissing,
    NodeCapacityExhausted,
    DeploymentCapacityExhausted,
    PortUnavailable,
    RdpWorkspaceBusy,
    GpuInventoryUnavailable,
    PinnedGpuMissing,
    GpuBindingUnavailable,
    GpuMetricsUnknown,
    GpuMemoryExhausted,
    GpuComputeExhausted,
    GpuEncoderExhausted,
}

impl PlacementRejectionReason {
    fn parse(value: &str) -> Result<Self, StoreError> {
        match value {
            "application_disabled" => Ok(Self::ApplicationDisabled),
            "application_deleted" => Ok(Self::ApplicationDeleted),
            "deployment_disabled" => Ok(Self::DeploymentDisabled),
            "node_disabled" => Ok(Self::NodeDisabled),
            "node_draining" => Ok(Self::NodeDraining),
            "node_deleted" => Ok(Self::NodeDeleted),
            "device_disabled" => Ok(Self::DeviceDisabled),
            "device_deleted" => Ok(Self::DeviceDeleted),
            "node_not_ready" => Ok(Self::NodeNotReady),
            "node_disconnected" => Ok(Self::NodeDisconnected),
            "node_stale" => Ok(Self::NodeStale),
            "control_epoch_mismatch" => Ok(Self::ControlEpochMismatch),
            "public_endpoint_missing" => Ok(Self::PublicEndpointMissing),
            "inventory_missing" => Ok(Self::InventoryMissing),
            "deployment_not_ready" => Ok(Self::DeploymentNotReady),
            "deployment_generation_mismatch" => Ok(Self::DeploymentGenerationMismatch),
            "deployment_epoch_mismatch" => Ok(Self::DeploymentEpochMismatch),
            "deployment_endpoint_revision_mismatch" => Ok(Self::DeploymentEndpointRevisionMismatch),
            "deployment_revision_mismatch" => Ok(Self::DeploymentRevisionMismatch),
            "capability_missing" => Ok(Self::CapabilityMissing),
            "node_capacity_exhausted" => Ok(Self::NodeCapacityExhausted),
            "deployment_capacity_exhausted" => Ok(Self::DeploymentCapacityExhausted),
            "port_unavailable" => Ok(Self::PortUnavailable),
            "rdp_workspace_busy" => Ok(Self::RdpWorkspaceBusy),
            "gpu_inventory_unavailable" => Ok(Self::GpuInventoryUnavailable),
            "pinned_gpu_missing" => Ok(Self::PinnedGpuMissing),
            "gpu_binding_unavailable" => Ok(Self::GpuBindingUnavailable),
            "gpu_metrics_unknown" => Ok(Self::GpuMetricsUnknown),
            "gpu_memory_exhausted" => Ok(Self::GpuMemoryExhausted),
            "gpu_compute_exhausted" => Ok(Self::GpuComputeExhausted),
            "gpu_encoder_exhausted" => Ok(Self::GpuEncoderExhausted),
            _ => Err(StoreError::Database(px_pg::DatabaseError::Operation)),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct PlacementCandidate {
    pub rank: Option<u32>,
    pub deployment_id: Uuid,
    pub node_id: Uuid,
    pub gpu_key: Option<String>,
    pub eligible: bool,
    pub dominant_pressure_per_mille: Option<i64>,
    pub average_pressure_per_mille: Option<i64>,
    pub node_slots: i64,
    pub deployment_slots: i64,
    pub gpu_memory_headroom_bytes: Option<i64>,
    pub gpu_compute_headroom_per_mille: Option<i64>,
    pub gpu_encoder_headroom_per_mille: Option<i64>,
    pub rejection_reasons: Vec<PlacementRejectionReason>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct PlacementPreview {
    pub application_id: Uuid,
    pub evaluated_at: DateTime<Utc>,
    pub candidates: Vec<PlacementCandidate>,
}

#[derive(sqlx::FromRow)]
pub(crate) struct PlacementCandidateRow {
    pub evaluated_at: DateTime<Utc>,
    pub deployment_id: Uuid,
    pub node_id: Uuid,
    pub gpu_key: Option<String>,
    pub eligible: bool,
    pub dominant_pressure_per_mille: Option<i64>,
    pub average_pressure_per_mille: Option<i64>,
    pub node_slots: i64,
    pub deployment_slots: i64,
    pub gpu_memory_headroom_bytes: Option<i64>,
    pub gpu_compute_headroom_per_mille: Option<i64>,
    pub gpu_encoder_headroom_per_mille: Option<i64>,
    pub rejection_reasons: Vec<String>,
}

impl PlacementCandidateRow {
    pub(crate) fn view(self, rank: Option<u32>) -> Result<PlacementCandidate, StoreError> {
        Ok(PlacementCandidate {
            rank,
            deployment_id: self.deployment_id,
            node_id: self.node_id,
            gpu_key: self.gpu_key,
            eligible: self.eligible,
            dominant_pressure_per_mille: self.dominant_pressure_per_mille,
            average_pressure_per_mille: self.average_pressure_per_mille,
            node_slots: self.node_slots,
            deployment_slots: self.deployment_slots,
            gpu_memory_headroom_bytes: self.gpu_memory_headroom_bytes,
            gpu_compute_headroom_per_mille: self.gpu_compute_headroom_per_mille,
            gpu_encoder_headroom_per_mille: self.gpu_encoder_headroom_per_mille,
            rejection_reasons: self
                .rejection_reasons
                .iter()
                .map(|reason| PlacementRejectionReason::parse(reason))
                .collect::<Result<Vec<_>, _>>()?,
        })
    }
}
impl StartApplication {
    pub(crate) fn digest(&self, client: ClientType) -> Result<[u8; 32], StoreError> {
        if self.request_id.is_nil()
            || self.application_id.is_nil()
            || self.deployment_id.is_some_and(|id| id.is_nil())
            || client == ClientType::AdminWeb
        {
            return Err(StoreError::InvalidInput);
        }
        let mut hash = Sha256::new();
        hash.update(b"Pixels-Start-v1\0");
        hash.update(self.application_id.as_bytes());
        match self.deployment_id {
            Some(id) => {
                hash.update([1]);
                hash.update(id.as_bytes());
            }
            None => hash.update([0]),
        }
        hash.update(client.name().as_bytes());
        Ok(hash.finalize().into())
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ApplicationInstance {
    pub id: Uuid,
    pub application_id: Uuid,
    pub owner: ResourceOwner,
    pub client_type: String,
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct InstanceRow {
    pub id: Uuid,
    pub application_id: Uuid,
    pub node_id: Uuid,
    pub owner_user: Option<Uuid>,
    pub owner_guest: Option<Uuid>,
    pub client_type: String,
    pub request_hash: Vec<u8>,
    pub state: String,
    pub revision: i64,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub deployment_id: Uuid,
    pub launch_id: Uuid,
    pub desired_state: String,
    pub application_revision: i64,
    pub deployment_revision: i64,
    pub endpoint_revision: i64,
    pub port: i32,
    pub created_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
impl InstanceRow {
    pub(crate) fn view(&self) -> Result<ApplicationInstance, StoreError> {
        let owner = match (self.owner_user, self.owner_guest) {
            (Some(user_id), None) => ResourceOwner::User { user_id },
            (None, Some(guest_id)) => ResourceOwner::Guest { guest_id },
            _ => return Err(StoreError::Database(px_pg::DatabaseError::Operation)),
        };
        Ok(ApplicationInstance {
            id: self.id,
            application_id: self.application_id,
            owner,
            client_type: self.client_type.clone(),
            state: self.state.clone(),
            revision: self.revision,
            created_at: self.created_at,
            ended_at: self.ended_at,
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn request_digest_binds_application_placement_and_client_without_owner_fallback() {
        let mut request = StartApplication {
            request_id: Uuid::new_v4(),
            application_id: Uuid::new_v4(),
            deployment_id: None,
        };
        let first = request.digest(ClientType::Android).unwrap();
        request.request_id = Uuid::new_v4();
        assert_eq!(first, request.digest(ClientType::Android).unwrap());
        assert_ne!(first, request.digest(ClientType::Panel).unwrap());
        request.deployment_id = Some(Uuid::new_v4());
        assert_ne!(first, request.digest(ClientType::Android).unwrap());
        assert!(request.digest(ClientType::AdminWeb).is_err());
        request.deployment_id = Some(Uuid::nil());
        assert!(request.digest(ClientType::Android).is_err());
    }
}
