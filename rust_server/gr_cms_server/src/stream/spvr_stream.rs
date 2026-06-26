use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct SpvrStream {
    pub stream_id: String,

    #[serde(default)]
    pub stream_name: String,

    #[serde(default)]
    pub encoder_bps: i64,

    #[serde(default, deserialize_with = "gr_base::serde_as_bool")]
    pub audio_enabled: bool,

    #[serde(default, deserialize_with = "gr_base::serde_as_bool")]
    pub clipboard_enabled: bool,

    #[serde(default, deserialize_with = "gr_base::serde_as_bool")]
    pub only_viewing: bool,

    #[serde(default, deserialize_with = "gr_base::serde_as_bool")]
    pub show_max_window: bool,

    #[serde(default, deserialize_with = "gr_base::serde_as_bool")]
    pub split_windows: bool,

    #[serde(default, deserialize_with = "gr_base::serde_as_bool")]
    pub enable_p2p: bool,

    #[serde(default)]
    pub audio_capture_mode: String,

    #[serde(default)]
    pub stream_host: String,

    #[serde(default)]
    pub stream_port: i64,

    #[serde(default)]
    pub bg_color: i64,

    #[serde(default)]
    pub encoder_fps: i64,

    #[serde(default)]
    pub network_type: String,

    #[serde(default)]
    pub connect_type: String,

    #[serde(default)]
    pub device_id: String,

    #[serde(default)]
    pub device_random_pwd: String,

    #[serde(default)]
    pub device_safety_pwd: String,

    #[serde(default)]
    pub remote_device_id: String,

    #[serde(default)]
    pub remote_device_random_pwd: String,

    #[serde(default)]
    pub remote_device_safety_pwd: String,

    #[serde(default)]
    pub created_timestamp: i64,

    #[serde(default)]
    pub updated_timestamp: i64,

    #[serde(default)]
    pub desktop_name: String,

    #[serde(default)]
    pub os_version: String,
}
