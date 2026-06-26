use crate::config::spvr_server_config::SpvrServerConfig;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SpvrAccessInfo {
    pub spvr_srv_config: SpvrServerConfig,
    //pub relay_srv_config: Vec<SpvrServerConfig>,
}
