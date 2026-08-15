use crate::px_relay::RelayMessageType;
use crate::cms_client::CmsClientMessageType;
use crate::cms_panel::CmsPanelMessageType;
use crate::cms_relay::CmsRelayMessageType;
use crate::cms_service::CmsServiceMessageType;

pub mod grpc_relay;
pub mod px_relay;
pub mod cms_client;
pub mod cms_panel;
pub mod cms_relay;
pub mod cms_service;

impl PartialEq<RelayMessageType> for i32 {
    fn eq(&self, other: &RelayMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<CmsRelayMessageType> for i32 {
    fn eq(&self, other: &CmsRelayMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<CmsPanelMessageType> for i32 {
    fn eq(&self, other: &CmsPanelMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<CmsClientMessageType> for i32 {
    fn eq(&self, other: &CmsClientMessageType) -> bool {
        *self == (*other as i32)
    }
}

impl PartialEq<CmsServiceMessageType> for i32 {
    fn eq(&self, other: &CmsServiceMessageType) -> bool {
        *self == (*other as i32)
    }
}
