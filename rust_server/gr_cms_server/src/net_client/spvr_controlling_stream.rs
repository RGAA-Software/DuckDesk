use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

pub type SpvrControllingStreamPtr = Arc<Mutex<SpvrControllingStream>>;

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct SpvrControllingStream {
    device_id: String,
    remote_device_id: String,
    begin_timestamp: i64,
    network_type: String,
    send_data_bytes: i64,
    received_data_bytes: i64,
    video_encode_format: String,
}
