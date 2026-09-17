use crate::StoreError;
use chrono::{DateTime, Utc};
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum DeploymentTarget {
    GameHook {
        install_root: String,
    },
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Webview,
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Rdp,
}
impl DeploymentTarget {
    pub(crate) fn kind(&self) -> &'static str {
        match self {
            Self::GameHook { .. } => "game_hook",
            Self::Webview => "webview",
            Self::Rdp => "rdp",
        }
    }
    pub(crate) fn root(&self) -> Option<&str> {
        match self {
            Self::GameHook { install_root } => Some(install_root),
            _ => None,
        }
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentConfiguration {
    pub target: DeploymentTarget,
    pub gpu_key: Option<String>,
    pub capacity: u32,
    pub disabled: bool,
}
impl DeploymentConfiguration {
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if !(1..=64).contains(&self.capacity)
            || (self.target == DeploymentTarget::Rdp && self.capacity != 1)
        {
            return Err(StoreError::InvalidInput);
        }
        if self.gpu_key.as_ref().is_some_and(|key| {
            key.is_empty()
                || key.len() > 128
                || !key.bytes().all(|byte_value| {
                    byte_value.is_ascii_alphanumeric() || b"_.:-".contains(&byte_value)
                })
        }) {
            return Err(StoreError::InvalidInput);
        }
        if let Some(root) = self.target.root() {
            absolute_install_root(root)?;
        }
        Ok(())
    }
}
pub(crate) fn absolute_install_root(root: &str) -> Result<(), StoreError> {
    let bytes = root.as_bytes();
    if bytes.len() < 3
        || bytes.len() > 2048
        || !bytes[0].is_ascii_alphabetic()
        || &bytes[1..3] != b":\\"
    {
        return Err(StoreError::InvalidInput);
    }
    if bytes.len() > 3 {
        crate::application_model::windows_relative_components(&root[3..])?;
    }
    Ok(())
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PreparationFailure {
    MissingFiles,
    UnsupportedMode,
    BindingUnverified,
    InvalidConfiguration,
    DependencyUnavailable,
}
impl PreparationFailure {
    fn name(self) -> &'static str {
        match self {
            Self::MissingFiles => "missing_files",
            Self::UnsupportedMode => "unsupported_mode",
            Self::BindingUnverified => "binding_unverified",
            Self::InvalidConfiguration => "invalid_configuration",
            Self::DependencyUnavailable => "dependency_unavailable",
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum PreparationState {
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Pending,
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Ready,
    Failed {
        reason: PreparationFailure,
    },
}
impl PreparationState {
    pub(crate) fn fields(self) -> (&'static str, Option<&'static str>) {
        match self {
            Self::Pending => ("pending", None),
            Self::Ready => ("ready", None),
            Self::Failed { reason } => ("failed", Some(reason.name())),
        }
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentObservation {
    pub deployment_revision: i64,
    pub application_revision: i64,
    pub endpoint_revision: i64,
    pub sequence: u64,
    pub status: PreparationState,
}
impl DeploymentObservation {
    pub(crate) fn validate(&self) -> Result<i64, StoreError> {
        let sequence = i64::try_from(self.sequence).map_err(|_| StoreError::InvalidInput)?;
        if sequence < 1
            || self.deployment_revision < 1
            || self.application_revision < 1
            || self.endpoint_revision < 1
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(sequence)
    }
}
/// Last observed preparation state, not an admission decision. Consumers must recheck all
/// revisions, node connection/epoch/freshness, enabled flags, reconciliation and capacity.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct DeploymentProfile {
    pub id: Uuid,
    pub application_id: Uuid,
    pub node_id: Uuid,
    pub kind: String,
    pub install_root: Option<String>,
    pub gpu_key: Option<String>,
    pub capacity: i32,
    pub disabled: bool,
    pub revision: i64,
    pub application_revision: i64,
    pub observed_state: String,
    pub observed_reason: Option<String>,
    pub observed_generation: Option<i64>,
    pub observed_epoch: Option<i64>,
    pub observed_endpoint_revision: Option<i64>,
    pub observed_sequence: i64,
    pub observed_at: Option<DateTime<Utc>>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum NodeDeploymentPreparation {
    GameHook {
        install_root: String,
        executable_relative: String,
        gpu_key: Option<String>,
    },
    Webview {
        gpu_key: Option<String>,
    },
    Rdp {
        gpu_key: Option<String>,
    },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NodeDeploymentAssignment {
    pub id: Uuid,
    pub application_id: Uuid,
    pub deployment_revision: i64,
    pub application_revision: i64,
    pub disabled: bool,
    pub preparation: NodeDeploymentPreparation,
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn deployment_paths_capacity_and_binding_are_explicit_and_bounded() {
        let mut config = DeploymentConfiguration {
            target: DeploymentTarget::GameHook {
                install_root: r"D:\游戏 目录".into(),
            },
            gpu_key: Some("GPU-001:0".into()),
            capacity: 4,
            disabled: false,
        };
        assert!(config.validate().is_ok());
        for root in [
            r"relative\path",
            r"\\server\share",
            r"C:relative",
            r"D:\..\secret",
            r"D:\CON",
            r"D:\bad.",
            r"D:\bad ",
            r"D:\a/b",
            r"D:\a*",
            r"D:\a:stream",
            "D:\\a\nb",
        ] {
            config.target = DeploymentTarget::GameHook {
                install_root: root.into(),
            };
            assert!(config.validate().is_err(), "{root}");
        }
        config.target = DeploymentTarget::Rdp;
        assert!(config.validate().is_err());
        config.capacity = 1;
        assert!(config.validate().is_ok());
        config.gpu_key = Some("".into());
        assert!(config.validate().is_err());
        config.gpu_key = None;
        config.capacity = u32::MAX;
        assert!(config.validate().is_err());
        let observation = DeploymentObservation {
            deployment_revision: 1,
            application_revision: 1,
            endpoint_revision: 1,
            sequence: u64::MAX,
            status: PreparationState::Ready,
        };
        assert!(observation.validate().is_err());
    }
}
