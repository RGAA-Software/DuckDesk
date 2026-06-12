use serde::{Deserialize, Serialize};
use crate::config::spvr_server_config::SpvrServerConfig;

#[derive(Debug, Serialize, Deserialize, Clone)]
#[derive(Default)]
pub struct SpvrAccessInfo {
    pub spvr_srv_config: SpvrServerConfig,
    //pub relay_srv_config: Vec<SpvrServerConfig>,
}