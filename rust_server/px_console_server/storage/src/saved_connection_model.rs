use crate::StoreError;
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum SavedConnectionTarget {
    Desktop { device_id: Uuid },
    CloudApplication { application_id: Uuid },
}
impl SavedConnectionTarget {
    pub(crate) fn ids(&self) -> Result<(Option<Uuid>, Option<Uuid>), StoreError> {
        let (id, desktop) = match self {
            Self::Desktop { device_id } => (*device_id, true),
            Self::CloudApplication { application_id } => (*application_id, false),
        };
        if id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        Ok(if desktop {
            (Some(id), None)
        } else {
            (None, Some(id))
        })
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AudioCapturePreference {
    SystemMix,
    TargetApplication,
}
impl AudioCapturePreference {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::SystemMix => "system_mix",
            Self::TargetApplication => "target_application",
        }
    }
}
/// Preferences only. They never grant access or override mode/capability negotiation.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SavedConnectionSettings {
    pub name: String,
    pub video_bitrate_bps: u32,
    pub video_fps: u32,
    pub audio_enabled: bool,
    pub clipboard_enabled: bool,
    pub view_only: bool,
    pub maximize: bool,
    pub split_windows: bool,
    pub prefer_peer_to_peer: bool,
    pub audio_capture: AudioCapturePreference,
    pub background_rgb: u32,
}
impl SavedConnectionSettings {
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if !(1..=64).contains(&self.name.chars().count())
            || self.name.trim() != self.name
            || self.name.chars().any(char::is_control)
            || !(256_000..=200_000_000).contains(&self.video_bitrate_bps)
            || !(1..=240).contains(&self.video_fps)
            || self.background_rgb > 0xff_ffff
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CreateSavedConnection {
    pub request_id: Uuid,
    pub target: SavedConnectionTarget,
    pub settings: SavedConnectionSettings,
}
impl CreateSavedConnection {
    pub(crate) fn digest(&self) -> Result<[u8; 32], StoreError> {
        if self.request_id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        self.settings.validate()?;
        let (device, app) = self.target.ids()?;
        let mut hash = Sha256::new();
        hash.update(b"Pixels-SavedConnection-v1\0");
        match (device, app) {
            (Some(id), None) => {
                hash.update([0]);
                hash.update(id.as_bytes());
            }
            (None, Some(id)) => {
                hash.update([1]);
                hash.update(id.as_bytes());
            }
            _ => return Err(StoreError::InvalidInput),
        }
        let s = &self.settings;
        hash.update(s.name.as_bytes());
        hash.update([0]);
        hash.update(s.video_bitrate_bps.to_be_bytes());
        hash.update(s.video_fps.to_be_bytes());
        hash.update([
            s.audio_enabled as u8,
            s.clipboard_enabled as u8,
            s.view_only as u8,
            s.maximize as u8,
            s.split_windows as u8,
            s.prefer_peer_to_peer as u8,
        ]);
        hash.update(s.audio_capture.name().as_bytes());
        hash.update([0]);
        hash.update(s.background_rgb.to_be_bytes());
        Ok(hash.finalize().into())
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct SavedConnection {
    pub id: Uuid,
    pub owner_id: Uuid,
    pub client_type: String,
    pub target: SavedConnectionTarget,
    pub settings: SavedConnectionSettings,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub deleted_at: Option<DateTime<Utc>>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct SavedConnectionRow {
    pub id: Uuid,
    pub owner_id: Uuid,
    pub client_type: String,
    pub request_id: Uuid,
    pub request_hash: Vec<u8>,
    pub name: String,
    pub device_id: Option<Uuid>,
    pub application_id: Option<Uuid>,
    pub video_bitrate_bps: i64,
    pub video_fps: i32,
    pub audio_enabled: bool,
    pub clipboard_enabled: bool,
    pub view_only: bool,
    pub maximize: bool,
    pub split_windows: bool,
    pub prefer_peer_to_peer: bool,
    pub audio_capture: String,
    pub background_rgb: i32,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub deleted_at: Option<DateTime<Utc>>,
}
impl SavedConnectionRow {
    pub(crate) fn target(&self) -> Result<SavedConnectionTarget, StoreError> {
        match (self.device_id, self.application_id) {
            (Some(device_id), None) => Ok(SavedConnectionTarget::Desktop { device_id }),
            (None, Some(application_id)) => {
                Ok(SavedConnectionTarget::CloudApplication { application_id })
            }
            _ => Err(StoreError::Rejected),
        }
    }
    pub(crate) fn view(&self) -> Result<SavedConnection, StoreError> {
        let settings = SavedConnectionSettings {
            name: self.name.clone(),
            video_bitrate_bps: self
                .video_bitrate_bps
                .try_into()
                .map_err(|_| StoreError::Rejected)?,
            video_fps: self
                .video_fps
                .try_into()
                .map_err(|_| StoreError::Rejected)?,
            audio_enabled: self.audio_enabled,
            clipboard_enabled: self.clipboard_enabled,
            view_only: self.view_only,
            maximize: self.maximize,
            split_windows: self.split_windows,
            prefer_peer_to_peer: self.prefer_peer_to_peer,
            audio_capture: match self.audio_capture.as_str() {
                "system_mix" => AudioCapturePreference::SystemMix,
                "target_application" => AudioCapturePreference::TargetApplication,
                _ => return Err(StoreError::Rejected),
            },
            background_rgb: self
                .background_rgb
                .try_into()
                .map_err(|_| StoreError::Rejected)?,
        };
        settings.validate()?;
        Ok(SavedConnection {
            id: self.id,
            owner_id: self.owner_id,
            client_type: self.client_type.clone(),
            target: self.target()?,
            settings,
            revision: self.revision,
            created_at: self.created_at,
            updated_at: self.updated_at,
            deleted_at: self.deleted_at,
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn preferences_are_bounded_explicit_and_request_hash_covers_every_setting() {
        let settings = SavedConnectionSettings {
            name: "远程 桌面".into(),
            video_bitrate_bps: 10_000_000,
            video_fps: 60,
            audio_enabled: true,
            clipboard_enabled: true,
            view_only: false,
            maximize: false,
            split_windows: false,
            prefer_peer_to_peer: true,
            audio_capture: AudioCapturePreference::SystemMix,
            background_rgb: 0,
        };
        let base = CreateSavedConnection {
            request_id: Uuid::new_v4(),
            target: SavedConnectionTarget::Desktop {
                device_id: Uuid::new_v4(),
            },
            settings,
        };
        let digest = base.digest().unwrap();
        let mut variants = Vec::new();
        macro_rules! change {
            ($field:ident,$value:expr) => {{
                let mut other = base.clone();
                other.settings.$field = $value;
                variants.push(other);
            }};
        }
        change!(name, "另一个".into());
        change!(video_bitrate_bps, 20_000_000);
        change!(video_fps, 120);
        change!(audio_enabled, false);
        change!(clipboard_enabled, false);
        change!(view_only, true);
        change!(maximize, true);
        change!(split_windows, true);
        change!(prefer_peer_to_peer, false);
        change!(audio_capture, AudioCapturePreference::TargetApplication);
        change!(background_rgb, 0xff_ffff);
        let mut other = base.clone();
        other.target = SavedConnectionTarget::CloudApplication {
            application_id: Uuid::new_v4(),
        };
        variants.push(other);
        for variant in variants {
            assert_ne!(digest, variant.digest().unwrap());
        }
        let mut invalid = base.clone();
        invalid.settings.video_fps = 0;
        assert!(invalid.digest().is_err());
        invalid = base.clone();
        invalid.settings.video_bitrate_bps = u32::MAX;
        assert!(invalid.digest().is_err());
        invalid = base.clone();
        invalid.settings.background_rgb = 0x100_0000;
        assert!(invalid.digest().is_err());
        invalid = base.clone();
        invalid.settings.name = " x".into();
        assert!(invalid.digest().is_err());
        invalid = base.clone();
        invalid.request_id = Uuid::nil();
        assert!(invalid.digest().is_err());
        invalid = base;
        invalid.target = SavedConnectionTarget::Desktop {
            device_id: Uuid::nil(),
        };
        assert!(invalid.digest().is_err());
    }
}
