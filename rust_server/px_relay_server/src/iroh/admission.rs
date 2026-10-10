use std::{collections::HashSet, sync::Mutex};

use iroh_base::EndpointId;
use iroh_relay::server::{Access, AccessControl, ClientRequest, ConnectionId};

#[derive(Debug, Default)]
struct Connections {
    draining: bool,
    active: HashSet<ConnectionId>,
}

#[derive(Debug)]
pub(super) struct RelayAdmission {
    limit: usize,
    connections: Mutex<Connections>,
}

impl RelayAdmission {
    pub(super) fn new(limit: usize) -> Self {
        Self {
            limit,
            connections: Mutex::new(Connections::default()),
        }
    }

    pub(super) fn set_draining(&self, draining: bool) {
        self.connections
            .lock()
            .expect("Relay admission mutex poisoned")
            .draining = draining;
    }

    pub(super) fn snapshot(&self) -> (bool, usize) {
        let connections = self
            .connections
            .lock()
            .expect("Relay admission mutex poisoned");
        (connections.draining, connections.active.len())
    }
}

impl AccessControl for RelayAdmission {
    async fn on_connect(&self, request: &ClientRequest) -> Access {
        let mut connections = self
            .connections
            .lock()
            .expect("Relay admission mutex poisoned");
        if connections.draining {
            return Access::Deny {
                reason: Some("Relay is draining".into()),
            };
        }
        if connections.active.len() >= self.limit {
            return Access::Deny {
                reason: Some("Relay connection capacity reached".into()),
            };
        }
        connections.active.insert(request.connection_id());
        Access::Allow
    }

    fn on_disconnect(&self, _endpoint_id: EndpointId, connection_id: ConnectionId) {
        self.connections
            .lock()
            .expect("Relay admission mutex poisoned")
            .active
            .remove(&connection_id);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iroh_base::SecretKey;
    use iroh_relay::http::ProtocolVersion;
    use std::sync::Arc;

    fn request() -> ClientRequest {
        ClientRequest::new(
            SecretKey::from_bytes(&[7; 32]).public(),
            ProtocolVersion::V2,
            http::Request::new(()).into_parts().0,
        )
    }

    #[tokio::test]
    async fn concurrent_connections_respect_capacity_and_exact_disconnect() {
        let admission = Arc::new(RelayAdmission::new(2));
        let mut tasks = tokio::task::JoinSet::new();
        for _attempt in 0..20 {
            let admission = admission.clone();
            tasks.spawn(async move {
                let request = request();
                let accepted = admission.on_connect(&request).await == Access::Allow;
                (request, accepted)
            });
        }
        let mut accepted_requests = Vec::new();
        while let Some(completed) = tasks.join_next().await {
            let (request, accepted) = completed.unwrap();
            if accepted {
                accepted_requests.push(request);
            }
        }
        assert_eq!(accepted_requests.len(), 2);
        assert_eq!(admission.snapshot(), (false, 2));
        let departing = &accepted_requests[0];
        admission.on_disconnect(departing.endpoint_id(), departing.connection_id());
        admission.on_disconnect(departing.endpoint_id(), departing.connection_id());
        assert_eq!(admission.snapshot(), (false, 1));
        assert_eq!(admission.on_connect(&request()).await, Access::Allow);
        assert_eq!(admission.snapshot(), (false, 2));
    }

    #[tokio::test]
    async fn draining_retains_existing_connections_and_can_resume() {
        let admission = RelayAdmission::new(2);
        let connected = request();
        assert_eq!(admission.on_connect(&connected).await, Access::Allow);
        admission.set_draining(true);
        assert!(matches!(
            admission.on_connect(&request()).await,
            Access::Deny { .. }
        ));
        assert_eq!(admission.snapshot(), (true, 1));
        admission.on_disconnect(connected.endpoint_id(), connected.connection_id());
        assert_eq!(admission.snapshot(), (true, 0));
        admission.set_draining(false);
        assert_eq!(admission.on_connect(&request()).await, Access::Allow);
    }
}
