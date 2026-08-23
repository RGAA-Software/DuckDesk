use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::Mutex;
use uuid::Uuid;

const PLAY_TICKET_IDLE_TTL: Duration = Duration::from_secs(5 * 60);

struct LivePlayTicket {
    stream_id: String,
    expires_at: Instant,
}

/// Short-lived opaque tickets used by the Console HLS relay.
///
/// A ticket is scoped to exactly one stream and is refreshed while the player
/// keeps fetching HLS segments. This avoids exposing either the ZLM HTTP API
/// secret or its direct playback URL to the Console web application.
pub struct ConsoleLiveTicketManager {
    tickets: Mutex<HashMap<String, LivePlayTicket>>,
}

impl ConsoleLiveTicketManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            tickets: Mutex::new(HashMap::new()),
        })
    }

    pub async fn issue(&self, stream_id: String) -> String {
        let token = Uuid::new_v4().to_string();
        let now = Instant::now();
        let mut tickets = self.tickets.lock().await;
        tickets.retain(|_, ticket| ticket.expires_at > now);
        tickets.insert(
            token.clone(),
            LivePlayTicket {
                stream_id,
                expires_at: now + PLAY_TICKET_IDLE_TTL,
            },
        );
        token
    }

    /// Validate and refresh one active HLS session.
    pub async fn validate(&self, token: &str, stream_id: &str) -> bool {
        let now = Instant::now();
        let mut tickets = self.tickets.lock().await;
        tickets.retain(|_, ticket| ticket.expires_at > now);
        let Some(ticket) = tickets.get_mut(token) else {
            return false;
        };
        if ticket.stream_id != stream_id {
            return false;
        }
        ticket.expires_at = now + PLAY_TICKET_IDLE_TTL;
        true
    }
}
