pub mod manager;
pub mod handler;
pub mod router;
pub mod store;

pub use manager::AppScheduleManager;

use lazy_static::lazy_static;
use std::sync::Arc;

lazy_static! {
    pub static ref gAppScheduleManager: Arc<AppScheduleManager> = Arc::new(AppScheduleManager::new());
}
