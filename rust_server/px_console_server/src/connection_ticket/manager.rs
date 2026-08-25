use crate::connection_ticket::model::{ConnectionTicket, TicketGrant};
use crate::console_api_error::ConsoleApiError;
use crate::event::audit;
use crate::gConsoleDatabase;
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use mongodb::bson::{doc, Bson, DateTime};
use mongodb::options::ReturnDocument;
use px_base::hash_util::{compute_hash, HashAlgo};
use ring::rand::{SecureRandom, SystemRandom};

pub struct ConnectionTicketManager;

impl ConnectionTicketManager {
    fn validate_nonce(value: &str) -> Result<(), ConsoleApiError> {
        if value.is_empty()
            || value.len() > 128
            || !value
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
        {
            return Err(ConsoleApiError::InvalidParams);
        }
        Ok(())
    }

    fn new_token() -> Result<String, ConsoleApiError> {
        let mut bytes = [0_u8; 32];
        SystemRandom::new()
            .fill(&mut bytes)
            .map_err(|_| ConsoleApiError::InternalError)?;
        Ok(URL_SAFE_NO_PAD.encode(bytes))
    }

    pub(crate) fn hash(token: &str) -> String {
        compute_hash(HashAlgo::SHA256, token.as_bytes())
    }

    #[allow(clippy::too_many_arguments)]
    pub async fn issue(
        kind: &str,
        subject_type: &str,
        subject_id: &str,
        session_id: &str,
        device_id: &str,
        app_id: Option<String>,
        instance_id: Option<String>,
        permissions: Vec<String>,
        client_nonce: String,
    ) -> Result<(String, String, ConnectionTicket), ConsoleApiError> {
        Self::validate_nonce(&client_nonce)?;
        if !matches!(kind, "device" | "app_instance")
            || !matches!(subject_type, "guest" | "user" | "admin")
            || device_id.is_empty()
            || session_id.is_empty()
        {
            return Err(ConsoleApiError::InvalidParams);
        }
        let raw = Self::new_token()?;
        let renewal_raw = Self::new_token()?;
        let now = px_base::get_current_timestamp();
        let ttl_seconds = crate::gConsoleSettings
            .lock()
            .await
            .user
            .ticket_expire_seconds
            .clamp(5, 300);
        let expires_at = now + ttl_seconds * 1000;
        // A renewal capability is rotated on every use and remains bounded by
        // the server-side user session check and an absolute configured TTL.
        let renewal_ttl_seconds = crate::gConsoleSettings
            .lock()
            .await
            .user
            .ticket_renew_expire_seconds
            .clamp(60, 7 * 24 * 60 * 60);
        let renewal_expires_at = now + renewal_ttl_seconds * 1000;
        let ticket = ConnectionTicket {
            ticket_hash: Self::hash(&raw),
            renewal_hash: Self::hash(&renewal_raw),
            kind: kind.to_string(),
            subject_type: subject_type.to_string(),
            subject_id: subject_id.to_string(),
            session_id: session_id.to_string(),
            device_id: device_id.to_string(),
            app_id,
            instance_id,
            permissions,
            client_nonce,
            created_at: now,
            expires_at,
            renewal_expires_at,
            cleanup_at: DateTime::from_millis(renewal_expires_at),
            consumed_at: None,
            consumed_request_id: None,
        };
        gConsoleDatabase
            .lock()
            .await
            .connection_ticket()
            .lock()
            .await
            .insert_one(ticket.clone())
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?;
        Ok((raw, renewal_raw, ticket))
    }

    /// Look up a renewal grant without consuming it. The following `renew`
    /// call still uses an atomic hash match, so concurrent attempts cannot both
    /// rotate the same capability successfully.
    pub async fn lookup_renewal(
        raw: &str,
        client_nonce: &str,
    ) -> Result<ConnectionTicket, ConsoleApiError> {
        Self::validate_nonce(client_nonce)?;
        let now = px_base::get_current_timestamp();
        gConsoleDatabase
            .lock()
            .await
            .connection_ticket()
            .lock()
            .await
            .find_one(doc! {
                "renewal_hash": Self::hash(raw),
                "client_nonce": client_nonce,
                "renewal_expires_at": { "$gt": now },
            })
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .ok_or(ConsoleApiError::TicketExpiredOrUsed)
    }

    pub async fn renew(
        raw: &str,
        client_nonce: &str,
    ) -> Result<(String, String, ConnectionTicket), ConsoleApiError> {
        Self::validate_nonce(client_nonce)?;
        let existing = Self::lookup_renewal(raw, client_nonce).await?;
        let now = px_base::get_current_timestamp();
        let session_active = gConsoleDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .find_one(doc! {
                "sid": &existing.session_id,
                "subject_type": &existing.subject_type,
                "subject_id": &existing.subject_id,
                "revoked_at": null,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .is_some();
        if !session_active {
            return Err(ConsoleApiError::TicketExpiredOrUsed);
        }

        let ticket_raw = Self::new_token()?;
        let renewal_raw = Self::new_token()?;
        let ttl_seconds = crate::gConsoleSettings
            .lock()
            .await
            .user
            .ticket_expire_seconds
            .clamp(5, 300);
        let expires_at = now + ttl_seconds * 1000;
        let renewed = gConsoleDatabase
            .lock()
            .await
            .connection_ticket()
            .lock()
            .await
            .find_one_and_update(
                doc! {
                    "renewal_hash": Self::hash(raw),
                    "client_nonce": client_nonce,
                    "renewal_expires_at": { "$gt": now },
                },
                doc! { "$set": {
                    "ticket_hash": Self::hash(&ticket_raw),
                    "renewal_hash": Self::hash(&renewal_raw),
                    "created_at": now,
                    "expires_at": expires_at,
                    "consumed_at": Bson::Null,
                    "consumed_request_id": Bson::Null,
                }},
            )
            .return_document(ReturnDocument::After)
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .ok_or(ConsoleApiError::TicketExpiredOrUsed)?;
        Ok((ticket_raw, renewal_raw, renewed))
    }

    pub async fn redeem(
        raw: &str,
        bound_device_id: &str,
        client_nonce: &str,
        instance_id: Option<&str>,
        request_id: &str,
    ) -> Result<TicketGrant, ConsoleApiError> {
        Self::validate_nonce(client_nonce)?;
        if request_id.is_empty()
            || request_id.len() > 128
            || request_id.chars().any(char::is_control)
        {
            return Err(ConsoleApiError::InvalidParams);
        }
        let now = px_base::get_current_timestamp();
        let ticket_hash = Self::hash(raw);
        let ticket_hash_prefix = &ticket_hash[..ticket_hash.len().min(8)];
        let mut filter = doc! {
            "ticket_hash": &ticket_hash,
            "device_id": bound_device_id,
            "client_nonce": client_nonce,
            "consumed_at": null,
            "expires_at": { "$gt": now },
        };
        match instance_id {
            Some(value) => {
                filter.insert("instance_id", value);
            }
            None => {
                filter.insert("instance_id", Bson::Null);
            }
        }
        let ticket_collection = gConsoleDatabase.lock().await.connection_ticket().clone();
        let ticket = ticket_collection
            .lock()
            .await
            .find_one_and_update(
                filter,
                doc! { "$set": {
                    "consumed_at": now,
                    "consumed_request_id": request_id,
                }},
            )
            .return_document(ReturnDocument::After)
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?;
        // A Direct RTC offer can be authorized successfully and then return
        // "occupied". Its immediate takeover retry is the same logical
        // redemption, not a second bearer use. Permit only an exact binding +
        // request-id replay, and only for a small window. A different request
        // id still observes strict one-time ticket semantics.
        let ticket = match ticket {
            Some(ticket) => Some(ticket),
            None => {
                let mut retry_filter = doc! {
                    "ticket_hash": &ticket_hash,
                    "device_id": bound_device_id,
                    "client_nonce": client_nonce,
                    "consumed_request_id": request_id,
                    "consumed_at": { "$gt": now - 30_000_i64 },
                    "expires_at": { "$gt": now },
                };
                match instance_id {
                    Some(value) => {
                        retry_filter.insert("instance_id", value);
                    }
                    None => {
                        retry_filter.insert("instance_id", Bson::Null);
                    }
                }
                ticket_collection
                    .lock()
                    .await
                    .find_one(retry_filter)
                    .await
                    .map_err(|_| ConsoleApiError::DatabaseError)?
            }
        };
        let Some(ticket) = ticket else {
            audit::record(
                "unknown",
                "",
                "ticket_redeem",
                "failure",
                "device",
                bound_device_id,
                &format!("request_id={request_id} ticket_hash={ticket_hash_prefix} reason=invalid_expired_used_or_binding_mismatch"),
            )
            .await;
            return Err(ConsoleApiError::TicketExpiredOrUsed);
        };

        let session_active = gConsoleDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .find_one(doc! {
                "sid": &ticket.session_id,
                "subject_type": &ticket.subject_type,
                "subject_id": &ticket.subject_id,
                "revoked_at": null,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .is_some();
        if !session_active {
            audit::record(
                &ticket.subject_type,
                &ticket.subject_id,
                "ticket_redeem",
                "failure",
                &ticket.kind,
                ticket.instance_id.as_deref().unwrap_or(&ticket.device_id),
                &format!("request_id={request_id} ticket_hash={ticket_hash_prefix} reason=session_inactive"),
            )
            .await;
            return Err(ConsoleApiError::TicketExpiredOrUsed);
        }
        audit::record(
            &ticket.subject_type,
            &ticket.subject_id,
            "ticket_redeem",
            "success",
            &ticket.kind,
            ticket.instance_id.as_deref().unwrap_or(&ticket.device_id),
            &format!("request_id={request_id} ticket_hash={ticket_hash_prefix}"),
        )
        .await;
        Ok(TicketGrant {
            kind: ticket.kind,
            device_id: ticket.device_id,
            app_id: ticket.app_id,
            instance_id: ticket.instance_id,
            subject_type: ticket.subject_type,
            subject_id: ticket.subject_id,
            permissions: ticket.permissions,
            expires_at: ticket.expires_at,
        })
    }

    /// Validate a short-lived ticket for a signaling transport without
    /// consuming it. The Render remains the sole consumer during offer
    /// handling, preserving the one-time capability boundary.
    pub async fn lookup_active(
        raw: &str,
        bound_device_id: &str,
        client_nonce: &str,
        instance_id: Option<&str>,
    ) -> Result<ConnectionTicket, ConsoleApiError> {
        Self::validate_nonce(client_nonce)?;
        let now = px_base::get_current_timestamp();
        let mut filter = doc! {
            "ticket_hash": Self::hash(raw),
            "device_id": bound_device_id,
            "client_nonce": client_nonce,
            "consumed_at": null,
            "expires_at": { "$gt": now },
        };
        match instance_id.filter(|value| !value.is_empty()) {
            Some(value) => {
                filter.insert("instance_id", value);
            }
            None => {
                filter.insert("instance_id", Bson::Null);
            }
        }
        gConsoleDatabase
            .lock()
            .await
            .connection_ticket()
            .lock()
            .await
            .find_one(filter)
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .ok_or(ConsoleApiError::TicketExpiredOrUsed)
    }
}

#[cfg(test)]
mod tests {
    use super::ConnectionTicketManager;
    use crate::gConsoleDatabase;
    use crate::user::session::ConsoleUserSession;
    use mongodb::bson::DateTime;

    #[test]
    fn nonce_is_url_fragment_safe() {
        assert!(ConnectionTicketManager::validate_nonce("browser_1-abc.xyz").is_ok());
        assert!(ConnectionTicketManager::validate_nonce("bad&ticket=x").is_err());
        assert!(ConnectionTicketManager::validate_nonce("").is_err());
    }

    #[tokio::test(flavor = "multi_thread", worker_threads = 4)]
    #[ignore = "requires a local MongoDB; run explicitly as the L1 ticket gate"]
    async fn mongodb_atomic_redeem_renew_and_binding_gate() {
        let database_name = format!("db_gr_console_server_test_ticket_{}", std::process::id());
        crate::gConsoleSettings.lock().await.mongodb_url = "mongodb://127.0.0.1:27017/".to_string();
        {
            let mut database = gConsoleDatabase.lock().await;
            database.use_isolated_test_database(&database_name);
            assert!(
                database.init().await,
                "initialize isolated MongoDB test database"
            );
        }

        let now = px_base::get_current_timestamp();
        let session_id = format!("ticket-test-session-{}", std::process::id());
        let session = ConsoleUserSession {
            sid: session_id.clone(),
            token_hash: "test-only-token-hash".to_string(),
            subject_type: "user".to_string(),
            subject_id: "ticket-test-user".to_string(),
            auth_version: 1,
            client_type: "test".to_string(),
            created_at: now,
            last_used_at: now,
            expires_at: now + 3_600_000,
            absolute_expires_at: now + 3_600_000,
            cleanup_at: DateTime::from_millis(now + 3_600_000),
            revoked_at: None,
            csrf_hash: String::new(),
            ip_hash: String::new(),
            user_agent_hash: String::new(),
        };
        gConsoleDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .insert_one(session)
            .await
            .expect("insert isolated test session");

        let (redeem_raw, _, _) = ConnectionTicketManager::issue(
            "device",
            "user",
            "ticket-test-user",
            &session_id,
            "device-90",
            None,
            None,
            vec!["view".to_string()],
            "concurrent-redeem".to_string(),
        )
        .await
        .expect("issue concurrent redeem ticket");
        let mut redeem_tasks = Vec::new();
        for attempt in 0..20 {
            let raw = redeem_raw.clone();
            redeem_tasks.push(tokio::spawn(async move {
                ConnectionTicketManager::redeem(
                    &raw,
                    "device-90",
                    "concurrent-redeem",
                    None,
                    &format!("redeem-attempt-{attempt}"),
                )
                .await
                .is_ok()
            }));
        }
        let mut redeem_successes = 0;
        for task in redeem_tasks {
            redeem_successes += usize::from(task.await.expect("join redeem attempt"));
        }

        let (_, renewal_raw, _) = ConnectionTicketManager::issue(
            "device",
            "user",
            "ticket-test-user",
            &session_id,
            "device-90",
            None,
            None,
            vec!["view".to_string()],
            "concurrent-renew".to_string(),
        )
        .await
        .expect("issue concurrent renewal ticket");
        let mut renewal_tasks = Vec::new();
        for _ in 0..20 {
            let raw = renewal_raw.clone();
            renewal_tasks.push(tokio::spawn(async move {
                ConnectionTicketManager::renew(&raw, "concurrent-renew")
                    .await
                    .is_ok()
            }));
        }
        let mut renewal_successes = 0;
        for task in renewal_tasks {
            renewal_successes += usize::from(task.await.expect("join renewal attempt"));
        }

        let (bound_raw, _, _) = ConnectionTicketManager::issue(
            "app_instance",
            "user",
            "ticket-test-user",
            &session_id,
            "device-90",
            Some("app-1".to_string()),
            Some("instance-1".to_string()),
            vec!["view".to_string()],
            "binding-nonce".to_string(),
        )
        .await
        .expect("issue binding ticket");
        let wrong_device = ConnectionTicketManager::redeem(
            &bound_raw,
            "other-device",
            "binding-nonce",
            Some("instance-1"),
            "wrong-device",
        )
        .await;
        let wrong_nonce = ConnectionTicketManager::redeem(
            &bound_raw,
            "device-90",
            "other-nonce",
            Some("instance-1"),
            "wrong-nonce",
        )
        .await;
        let wrong_instance = ConnectionTicketManager::redeem(
            &bound_raw,
            "device-90",
            "binding-nonce",
            Some("other-instance"),
            "wrong-instance",
        )
        .await;
        let correct_binding = ConnectionTicketManager::redeem(
            &bound_raw,
            "device-90",
            "binding-nonce",
            Some("instance-1"),
            "correct-binding",
        )
        .await;

        let (idempotent_raw, _, _) = ConnectionTicketManager::issue(
            "device",
            "user",
            "ticket-test-user",
            &session_id,
            "device-90",
            None,
            None,
            vec!["view".to_string()],
            "takeover-nonce".to_string(),
        )
        .await
        .expect("issue idempotent takeover ticket");
        let first_takeover_attempt = ConnectionTicketManager::redeem(
            &idempotent_raw,
            "device-90",
            "takeover-nonce",
            None,
            "stable-takeover-redemption",
        )
        .await;
        let same_takeover_retry = ConnectionTicketManager::redeem(
            &idempotent_raw,
            "device-90",
            "takeover-nonce",
            None,
            "stable-takeover-redemption",
        )
        .await;
        let unrelated_replay = ConnectionTicketManager::redeem(
            &idempotent_raw,
            "device-90",
            "takeover-nonce",
            None,
            "different-redemption",
        )
        .await;

        let mongodb_url = crate::gConsoleSettings.lock().await.mongodb_url.clone();
        let client = mongodb::Client::with_uri_str(&mongodb_url)
            .await
            .expect("create cleanup MongoDB client");
        client
            .database(&database_name)
            .drop()
            .await
            .expect("drop isolated ticket test database");

        assert_eq!(
            redeem_successes, 1,
            "one-time ticket must redeem exactly once"
        );
        assert_eq!(
            renewal_successes, 1,
            "renewal capability must rotate exactly once"
        );
        assert!(wrong_device.is_err());
        assert!(wrong_nonce.is_err());
        assert!(wrong_instance.is_err());
        assert!(
            correct_binding.is_ok(),
            "failed binding attempts must not consume the ticket"
        );
        assert!(first_takeover_attempt.is_ok());
        assert!(
            same_takeover_retry.is_ok(),
            "an occupied Direct RTC allocation must be able to retry takeover idempotently"
        );
        assert!(
            unrelated_replay.is_err(),
            "a different redemption id must not replay a consumed ticket"
        );
    }
}
