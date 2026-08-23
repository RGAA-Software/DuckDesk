use crate::console_client::ConsoleClientMessageType;
use crate::console_panel::ConsolePanelMessageType;
use crate::console_relay::ConsoleRelayMessageType;
use crate::console_service::ConsoleServiceMessageType;
use crate::px_relay::RelayMessageType;

pub mod console_client;
pub mod console_panel;
pub mod console_relay;
pub mod console_service;
pub mod grpc_relay;
pub mod px_relay;

impl PartialEq<RelayMessageType> for i32 {
    fn eq(&self, other: &RelayMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<ConsoleRelayMessageType> for i32 {
    fn eq(&self, other: &ConsoleRelayMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<ConsolePanelMessageType> for i32 {
    fn eq(&self, other: &ConsolePanelMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<ConsoleClientMessageType> for i32 {
    fn eq(&self, other: &ConsoleClientMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<ConsoleServiceMessageType> for i32 {
    fn eq(&self, other: &ConsoleServiceMessageType) -> bool {
        *self == (*other as i32)
    }
}
