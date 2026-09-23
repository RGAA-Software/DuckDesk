use crate::{RuntimeEpoch, StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct RelayNodeSpec {
    pub name: String,
    pub public_host: String,
    pub public_port: u16,
}

impl RelayNodeSpec {
    pub(crate) fn validate(&self) -> Result<String, StoreError> {
        if self.name.is_empty()
            || self.name.len() > 80
            || self.name.trim() != self.name
            || self.name.chars().any(char::is_control)
            || self.public_host.is_empty()
            || self.public_host.len() > 253
            || self.public_host.chars().any(char::is_whitespace)
            || self.public_host.chars().any(char::is_control)
            || self.public_host.contains(['/', '\\', '@', '?', '#'])
            || self.public_port == 0
        {
            return Err(StoreError::InvalidInput);
        }
        if let Ok(address) = self.public_host.parse::<std::net::IpAddr>() {
            return Ok(address.to_string());
        }
        url::Host::parse(&self.public_host)
            .map(|host| host.to_string())
            .map_err(|_| StoreError::InvalidInput)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RelayNodeConfiguration {
    pub draining: bool,
    pub disabled: bool,
}

#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RelayNodeReport {
    pub sequence: u64,
    pub product_version_code: u32,
    pub draining: bool,
    pub max_connections: u32,
    pub current_connections: u32,
    pub max_rooms: u32,
    pub current_rooms: u32,
    pub uploaded_bytes: u64,
    pub forwarded_bytes: u64,
}

pub(crate) struct ValidatedRelayNodeReport {
    pub sequence: i64,
    pub product_version_code: i64,
    pub max_connections: i32,
    pub current_connections: i32,
    pub max_rooms: i32,
    pub current_rooms: i32,
    pub uploaded_bytes: i64,
    pub forwarded_bytes: i64,
}

impl RelayNodeReport {
    pub(crate) fn validate(&self) -> Result<ValidatedRelayNodeReport, StoreError> {
        if self.sequence == 0
            || self.product_version_code == 0
            || !(2..=100_000).contains(&self.max_connections)
            || self.current_connections > self.max_connections
            || !(1..=50_000).contains(&self.max_rooms)
            || self.current_rooms > self.max_rooms
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(ValidatedRelayNodeReport {
            sequence: i64::try_from(self.sequence).map_err(|_| StoreError::InvalidInput)?,
            product_version_code: i64::from(self.product_version_code),
            max_connections: i32::try_from(self.max_connections)
                .map_err(|_| StoreError::InvalidInput)?,
            current_connections: i32::try_from(self.current_connections)
                .map_err(|_| StoreError::InvalidInput)?,
            max_rooms: i32::try_from(self.max_rooms).map_err(|_| StoreError::InvalidInput)?,
            current_rooms: i32::try_from(self.current_rooms)
                .map_err(|_| StoreError::InvalidInput)?,
            uploaded_bytes: i64::try_from(self.uploaded_bytes)
                .map_err(|_| StoreError::InvalidInput)?,
            forwarded_bytes: i64::try_from(self.forwarded_bytes)
                .map_err(|_| StoreError::InvalidInput)?,
        })
    }
}

/// Server-side connection context created only after Relay credential authentication.
#[derive(Debug, Clone)]
pub struct RelayNodeConnection {
    pub(crate) id: Uuid,
    pub(crate) generation: i64,
    pub(crate) epoch: RuntimeEpoch,
    pub(crate) key: TokenDigest,
}

impl RelayNodeConnection {
    pub fn id(&self) -> Uuid {
        self.id
    }

    pub fn generation(&self) -> i64 {
        self.generation
    }

    pub fn epoch(&self) -> RuntimeEpoch {
        self.epoch
    }
}

/// Administrative inventory view. Fresh means recent authenticated contact; selection
/// must additionally require Ready, enabled and matching desired/reported drain state.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct RelayNodeProfile {
    pub id: Uuid,
    pub name: String,
    pub public_host: String,
    pub public_port: i32,
    pub revision: i64,
    pub generation: i64,
    pub control_epoch: Option<i64>,
    pub state: String,
    pub desired_draining: bool,
    pub reported_draining: Option<bool>,
    pub disabled: bool,
    pub report_sequence: i64,
    pub last_seen: Option<DateTime<Utc>>,
    pub product_version_code: Option<i64>,
    pub max_connections: Option<i32>,
    pub current_connections: Option<i32>,
    pub max_rooms: Option<i32>,
    pub current_rooms: Option<i32>,
    pub uploaded_bytes: Option<i64>,
    pub forwarded_bytes: Option<i64>,
    pub fresh: bool,
}
