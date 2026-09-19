use crate::console_relay::relay_conn::RelayConn;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{Mutex, RwLock};

fn remove_if_current<T>(
    connections: &mut HashMap<String, Arc<T>>,
    device_id: &str,
    connection: &Arc<T>,
) -> bool {
    let is_current = connections
        .get(device_id)
        .is_some_and(|current| Arc::ptr_eq(current, connection));
    if is_current {
        connections.remove(device_id);
    }
    is_current
}

pub struct RelayConnManager {
    pub relay_conns: RwLock<HashMap<String, Arc<Mutex<RelayConn>>>>,
}

impl RelayConnManager {
    pub fn new() -> RelayConnManager {
        RelayConnManager {
            relay_conns: RwLock::new(HashMap::new()),
        }
    }

    pub async fn add_connection(&self, device_id: String, relay_conn: Arc<Mutex<RelayConn>>) {
        self.relay_conns
            .write()
            .await
            .insert(device_id.clone(), relay_conn);
    }

    /// Removes a disconnected socket only when it is still the registered
    /// connection for this device. A replacement socket may already have been
    /// inserted while the old receive task is winding down.
    pub async fn remove_connection_if_current(
        &self,
        device_id: &str,
        relay_conn: &Arc<Mutex<RelayConn>>,
    ) -> bool {
        let mut connections = self.relay_conns.write().await;
        remove_if_current(&mut *connections, device_id, relay_conn)
    }

    pub async fn get_conn(&self, device_id: String) -> Option<Arc<Mutex<RelayConn>>> {
        self.relay_conns.read().await.get(&device_id).cloned()
    }

    pub async fn get_connections(&self) -> Vec<Arc<Mutex<RelayConn>>> {
        self.relay_conns.read().await.values().cloned().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::remove_if_current;
    use std::collections::HashMap;
    use std::sync::Arc;

    #[test]
    fn stale_disconnect_does_not_remove_replacement_connection() {
        let stale = Arc::new(1_u8);
        let replacement = Arc::new(2_u8);
        let mut connections = HashMap::from([("device".to_string(), replacement.clone())]);

        assert!(!remove_if_current(&mut connections, "device", &stale));
        assert!(Arc::ptr_eq(
            connections.get("device").expect("replacement must remain"),
            &replacement
        ));
    }

    #[test]
    fn current_disconnect_removes_registered_connection() {
        let current = Arc::new(1_u8);
        let mut connections = HashMap::from([("device".to_string(), current.clone())]);

        assert!(remove_if_current(&mut connections, "device", &current));
        assert!(!connections.contains_key("device"));
    }
}
