//! 接入凭据算法已上移到共享 crate `px_auth_mgr`（CMS 等设备端也需要签名）。
//! 本模块仅做 re-export，保持 `crate::app_credential` 路径不变。

pub use px_auth_mgr::app_credential::*;
