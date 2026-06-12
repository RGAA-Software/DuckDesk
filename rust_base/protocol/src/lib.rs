use crate::relay::RelayMessageType;
use crate::spvr_client::SpvrClientMessageType;
use crate::spvr_panel::SpvrPanelMessageType;
use crate::spvr_relay::SpvrRelayMessageType;

pub mod relay;
pub mod grpc_relay;
pub mod spvr_client;
pub mod spvr_panel;
pub mod spvr_relay;

impl PartialEq<RelayMessageType> for i32 {
    fn eq(&self, other: &RelayMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<SpvrRelayMessageType> for i32 {
    fn eq(&self, other: &SpvrRelayMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<SpvrPanelMessageType> for i32 {
    fn eq(&self, other: &SpvrPanelMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<SpvrClientMessageType> for i32 {
    fn eq(&self, other: &SpvrClientMessageType) -> bool {
        *self == (*other as i32)
    }
}