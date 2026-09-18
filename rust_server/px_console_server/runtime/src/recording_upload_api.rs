use crate::{error::ApiError, request::Route as Path, StateData};
use axum::{
    body::Body,
    extract::{OriginalUri, State},
    http::{header, HeaderMap, StatusCode},
    routing::put,
    Json, Router,
};
use futures_util::StreamExt;
use px_console_store::{CacheAttempt, CacheProfile, NodeConnection};
use px_node_protocol::RecordingCacheUpload;
use sha2::{Digest, Sha256};
use std::{
    collections::{HashMap, HashSet},
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};
use tokio::sync::{OwnedSemaphorePermit, Semaphore};
use uuid::Uuid;
use zeroize::Zeroizing;

const MAX_UPLOAD_GRANTS: usize = 128;
const MAX_UPLOADS: usize = 32;
const BODY_WAIT: Duration = Duration::from_secs(5);
const RENEW_BEFORE: Duration = Duration::from_secs(10);
const APPEND_BYTES: usize = 1024 * 1024;

struct PendingUpload {
    connection: NodeConnection,
    attempt: CacheAttempt,
    expires_at: Instant,
}

#[derive(Default)]
struct RegistryState {
    grants: HashMap<[u8; 32], PendingUpload>,
    attempts: HashMap<Uuid, [u8; 32]>,
    active: HashSet<Uuid>,
}

pub(crate) struct UploadRegistry {
    state: Mutex<RegistryState>,
    slots: Arc<Semaphore>,
}

struct ActiveUpload {
    registry: Arc<UploadRegistry>,
    attempt_id: Uuid,
}

impl Drop for ActiveUpload {
    fn drop(&mut self) {
        if let Ok(mut state) = self.registry.state.lock() {
            state.active.remove(&self.attempt_id);
        }
    }
}

pub(crate) struct RedeemedUpload {
    connection: NodeConnection,
    attempt: CacheAttempt,
    _active: ActiveUpload,
    _permit: OwnedSemaphorePermit,
}

impl UploadRegistry {
    pub(crate) fn new() -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(RegistryState::default()),
            slots: Arc::new(Semaphore::new(MAX_UPLOADS)),
        })
    }

    pub(crate) fn issue(
        self: &Arc<Self>,
        connection: &NodeConnection,
        attempt: CacheAttempt,
    ) -> Result<Option<RecordingCacheUpload>, ApiError> {
        let now = Instant::now();
        let mut state = self.state.lock().map_err(|_| ApiError::Unavailable)?;
        Self::prune(&mut state, now);
        if state.active.contains(&attempt.id()) {
            return Ok(None);
        }
        if state.attempts.contains_key(&attempt.id()) {
            return Ok(None);
        }
        if state.grants.len() >= MAX_UPLOAD_GRANTS {
            return Err(ApiError::Unavailable);
        }
        let (token, _storage_token_digest) = crate::request::mint();
        let token_digest = Self::digest(&token);
        let valid_for_ms = attempt.valid_for_ms();
        if valid_for_ms == 0 {
            return Ok(None);
        }
        let descriptor = RecordingCacheUpload {
            attempt_id: attempt.id(),
            recording_id: attempt.recording_id(),
            source_id: attempt.source_id(),
            size_bytes: attempt.content().size(),
            source_sha256: attempt.content().sha256(),
            upload_path: format!("/api/console/node-recording-cache/{}", attempt.id()),
            upload_token: token.to_string(),
            valid_for_ms,
        };
        let pending = PendingUpload {
            connection: connection.clone(),
            attempt,
            expires_at: now + Duration::from_millis(u64::from(valid_for_ms)),
        };
        state.attempts.insert(descriptor.attempt_id, token_digest);
        state.grants.insert(token_digest, pending);
        Ok(Some(descriptor))
    }

    fn redeem(self: &Arc<Self>, token: &str, attempt_id: Uuid) -> Result<RedeemedUpload, ApiError> {
        let digest = Self::digest(token);
        let permit = self
            .slots
            .clone()
            .try_acquire_owned()
            .map_err(|_| ApiError::RateLimited)?;
        let now = Instant::now();
        let mut state = self.state.lock().map_err(|_| ApiError::Unavailable)?;
        Self::prune(&mut state, now);
        let pending = state.grants.remove(&digest).ok_or(ApiError::Unauthorized)?;
        state.attempts.remove(&pending.attempt.id());
        if pending.expires_at <= now || pending.attempt.id() != attempt_id {
            return Err(ApiError::Unauthorized);
        }
        if !state.active.insert(attempt_id) {
            return Err(ApiError::Conflict);
        }
        Ok(RedeemedUpload {
            connection: pending.connection,
            attempt: pending.attempt,
            _active: ActiveUpload {
                registry: self.clone(),
                attempt_id,
            },
            _permit: permit,
        })
    }

    fn prune(state: &mut RegistryState, now: Instant) {
        let expired: Vec<_> = state
            .grants
            .iter()
            .filter_map(|(digest, grant)| (grant.expires_at <= now).then_some(*digest))
            .collect();
        for digest in expired {
            if let Some(grant) = state.grants.remove(&digest) {
                state.attempts.remove(&grant.attempt.id());
            }
        }
    }

    fn digest(token: &str) -> [u8; 32] {
        Sha256::digest(token.as_bytes()).into()
    }
}

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new().route("/api/console/node-recording-cache/{id}", put(upload))
}

async fn upload(
    State(state): State<Arc<StateData>>,
    OriginalUri(uri): OriginalUri,
    Path(attempt_id): Path<Uuid>,
    headers: HeaderMap,
    body: Body,
) -> Result<(StatusCode, Json<CacheProfile>), ApiError> {
    if uri.query().is_some()
        || headers.contains_key(header::ORIGIN)
        || headers.contains_key("x-pixels-client-type")
        || headers
            .keys()
            .any(|key| key.as_str() == "forwarded" || key.as_str().starts_with("x-forwarded-"))
        || headers.get_all(header::AUTHORIZATION).iter().count() != 1
        || headers.get_all(header::CONTENT_LENGTH).iter().count() != 1
        || headers
            .get(header::CONTENT_TYPE)
            .and_then(|value| value.to_str().ok())
            != Some("application/octet-stream")
    {
        return Err(ApiError::Rejected);
    }
    let token = headers
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .filter(|value| crate::request::secret_digest(value).is_some())
        .ok_or(ApiError::Unauthorized)?;
    let token = Zeroizing::new(token.to_string());
    let upload = state.uploads.redeem(&token, attempt_id)?;
    let expected_size = upload.attempt.content().size();
    let content_length = headers
        .get(header::CONTENT_LENGTH)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.parse::<u64>().ok())
        .ok_or(ApiError::Invalid)?;
    if content_length != expected_size {
        return Err(ApiError::Invalid);
    }
    let result = receive_upload(&state, body, &upload).await;
    if result.is_err() {
        if let Some(cache) = &state.recording_cache {
            let _ = state
                .db
                .recording_cache()
                .abandon(cache, &upload.connection, &upload.attempt)
                .await;
        }
    }
    result.map(|profile| (StatusCode::CREATED, Json(profile)))
}

async fn receive_upload(
    state: &StateData,
    body: Body,
    upload: &RedeemedUpload,
) -> Result<CacheProfile, ApiError> {
    let cache = state
        .recording_cache
        .as_ref()
        .ok_or(ApiError::Unavailable)?;
    let root = cache.root().clone();
    let attempt = upload.attempt.clone();
    let mut writer = tokio::task::spawn_blocking(move || {
        root.try_lock_blob(attempt.id())?
            .begin_write(attempt.content())
    })
    .await
    .map_err(|_| ApiError::Internal)?
    .map_err(|_| ApiError::Rejected)?;
    let renewed = state
        .db
        .recording_cache()
        .renew(cache, &upload.connection, &upload.attempt, &writer)
        .await?;
    let mut stream = body.into_data_stream();
    let mut lease_deadline =
        Instant::now() + Duration::from_millis(u64::from(renewed.valid_for_ms()));
    let mut received = 0_u64;
    loop {
        if lease_deadline.saturating_duration_since(Instant::now()) <= RENEW_BEFORE {
            let renewed = state
                .db
                .recording_cache()
                .renew(cache, &upload.connection, &upload.attempt, &writer)
                .await?;
            lease_deadline =
                Instant::now() + Duration::from_millis(u64::from(renewed.valid_for_ms()));
        }
        let next = tokio::select! {
            biased;
            _ = state.cancellation.cancelled() => return Err(ApiError::Unavailable),
            result = tokio::time::timeout(BODY_WAIT, stream.next()) => result.map_err(|_| ApiError::Unavailable)?,
        };
        let Some(chunk) = next else {
            break;
        };
        let chunk = chunk.map_err(|_| ApiError::Invalid)?;
        for part in chunk.chunks(APPEND_BYTES) {
            if part.is_empty() {
                continue;
            }
            received = received
                .checked_add(part.len() as u64)
                .ok_or(ApiError::Invalid)?;
            if received > upload.attempt.content().size() {
                return Err(ApiError::Invalid);
            }
            let bytes = part.to_vec();
            writer = tokio::task::spawn_blocking(move || {
                let mut writer = writer;
                let result = writer.append(&bytes);
                (writer, result)
            })
            .await
            .map_err(|_| ApiError::Internal)
            .and_then(|(writer, result)| result.map(|_| writer).map_err(|_| ApiError::Rejected))?;
        }
    }
    if received != upload.attempt.content().size() {
        return Err(ApiError::Invalid);
    }
    let proof = tokio::task::spawn_blocking(move || writer.finish())
        .await
        .map_err(|_| ApiError::Internal)?
        .map_err(|_| ApiError::Rejected)?;
    let profile = state
        .db
        .recording_cache()
        .publish(cache, &upload.connection, &upload.attempt, &proof)
        .await?;
    drop(proof);
    Ok(profile)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn upload_tokens_are_hashed_before_registry_lookup() {
        assert_eq!(UploadRegistry::digest("a"), UploadRegistry::digest("a"));
        assert_ne!(UploadRegistry::digest("a"), UploadRegistry::digest("b"));
    }
}
