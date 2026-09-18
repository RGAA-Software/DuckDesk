use crate::{
    error::ApiError,
    request::{self, Route as Path},
    StateData,
};
use axum::{
    body::{Body, Bytes},
    extract::State,
    http::{
        header::{
            ACCEPT_RANGES, CONTENT_DISPOSITION, CONTENT_LENGTH, CONTENT_RANGE, CONTENT_TYPE, RANGE,
        },
        HeaderMap, HeaderValue, StatusCode,
    },
    response::{IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use futures_util::stream;
use px_console_store::{
    CacheCredential, CacheProfile, CacheReadLease, CacheRuntime, ClientType, TokenDigest,
};
use px_private_files::BlobReader;
use std::{
    io::{Read, Seek, SeekFrom},
    sync::Arc,
    time::{Duration, Instant},
};
use tokio::sync::mpsc;
use uuid::Uuid;

const CHUNK_BYTES: u64 = 64 * 1024;
const SEND_WAIT: Duration = Duration::from_secs(5);
const RENEW_BEFORE: Duration = Duration::from_secs(10);

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/recordings/{id}/cache", post(request_cache))
        .route("/api/console/recordings/{id}/download", get(download))
        .route(
            "/api/console/managed/recordings/{id}/cache",
            post(request_managed_cache),
        )
        .route(
            "/api/console/managed/recordings/{id}/download",
            get(download_managed),
        )
}

enum CacheAuthorization {
    Managed(TokenDigest),
    User {
        token: TokenDigest,
        client: ClientType,
    },
}

impl CacheAuthorization {
    fn credential(&self) -> CacheCredential<'_> {
        match self {
            Self::Managed(token) => CacheCredential::Managed(token),
            Self::User { token, client } => CacheCredential::User {
                token,
                client: *client,
            },
        }
    }
}

async fn request_cache(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<CacheProfile>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    let authorization = CacheAuthorization::User {
        token: context.user_token()?.clone(),
        client: context.client,
    };
    request_authorized_cache(&state, id, &authorization).await
}

async fn request_managed_cache(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<CacheProfile>, ApiError> {
    let authorization = CacheAuthorization::Managed(request::administrator(&state, &headers)?);
    request_authorized_cache(&state, id, &authorization).await
}

async fn request_authorized_cache(
    state: &StateData,
    id: Uuid,
    authorization: &CacheAuthorization,
) -> Result<Json<CacheProfile>, ApiError> {
    let cache = state
        .recording_cache
        .as_ref()
        .ok_or(ApiError::Unavailable)?;
    Ok(Json(
        state
            .db
            .recording_cache()
            .request(cache, authorization.credential(), id)
            .await?,
    ))
}

async fn download(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Response, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    let authorization = CacheAuthorization::User {
        token: context.user_token()?.clone(),
        client: context.client,
    };
    download_authorized(state, headers, id, authorization).await
}

async fn download_managed(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Response, ApiError> {
    let authorization = CacheAuthorization::Managed(request::administrator(&state, &headers)?);
    download_authorized(state, headers, id, authorization).await
}

async fn download_authorized(
    state: Arc<StateData>,
    headers: HeaderMap,
    id: Uuid,
    authorization: CacheAuthorization,
) -> Result<Response, ApiError> {
    let cache = state
        .recording_cache
        .as_ref()
        .ok_or(ApiError::Unavailable)?;
    let store = state.db.recording_cache();
    let cached = store
        .cached_file(cache, authorization.credential(), id)
        .await?;
    let root = cache.root().clone();
    let blob_id = cached.id;
    let content = cached.content;
    let reader = tokio::task::spawn_blocking(move || root.try_read(blob_id, content))
        .await
        .map_err(|_| ApiError::Internal)?
        .map_err(|_| ApiError::Unavailable)?;
    store.verify_cached(cache, id, &reader).await?;
    let lease = store
        .open_read(cache, authorization.credential(), id, &reader)
        .await?;
    let size = content.size();
    let Some(range) = parse_range(&headers, size)? else {
        store.close_read(cache, &lease).await?;
        return Ok(range_error(size));
    };
    let length = range.end - range.start + 1;
    let receiver = spawn_reader(
        store,
        cache.clone(),
        lease,
        reader,
        range.start,
        length,
        state.cancellation.clone(),
    );
    let body_stream = stream::unfold(receiver, |mut receiver| async move {
        receiver.recv().await.map(|item| (item, receiver))
    });
    let mut response = Response::new(Body::from_stream(body_stream));
    *response.status_mut() = if range.partial {
        StatusCode::PARTIAL_CONTENT
    } else {
        StatusCode::OK
    };
    let response_headers = response.headers_mut();
    response_headers.insert(ACCEPT_RANGES, HeaderValue::from_static("bytes"));
    response_headers.insert(CONTENT_TYPE, HeaderValue::from_static("video/mp4"));
    response_headers.insert(
        CONTENT_DISPOSITION,
        HeaderValue::from_str(&format!("attachment; filename=\"recording-{id}.mp4\""))
            .map_err(|_| ApiError::Internal)?,
    );
    response_headers.insert(
        CONTENT_LENGTH,
        HeaderValue::from_str(&length.to_string()).map_err(|_| ApiError::Internal)?,
    );
    if range.partial {
        response_headers.insert(
            CONTENT_RANGE,
            HeaderValue::from_str(&format!("bytes {}-{}/{}", range.start, range.end, size))
                .map_err(|_| ApiError::Internal)?,
        );
    }
    Ok(response)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct ByteRange {
    start: u64,
    end: u64,
    partial: bool,
}

fn parse_range(headers: &HeaderMap, size: u64) -> Result<Option<ByteRange>, ApiError> {
    if size == 0 || headers.get_all(RANGE).iter().count() > 1 {
        return Err(ApiError::Invalid);
    }
    let Some(value) = headers.get(RANGE) else {
        return Ok(Some(ByteRange {
            start: 0,
            end: size - 1,
            partial: false,
        }));
    };
    let text = value.to_str().map_err(|_| ApiError::Invalid)?;
    let specification = text.strip_prefix("bytes=").ok_or(ApiError::Invalid)?;
    if specification.contains(',') {
        return Ok(None);
    }
    let (start, end) = specification.split_once('-').ok_or(ApiError::Invalid)?;
    let (start, end) = if start.is_empty() {
        let suffix = end.parse::<u64>().map_err(|_| ApiError::Invalid)?;
        if suffix == 0 {
            return Ok(None);
        }
        (size.saturating_sub(suffix), size - 1)
    } else {
        let start = start.parse::<u64>().map_err(|_| ApiError::Invalid)?;
        if start >= size {
            return Ok(None);
        }
        let end = if end.is_empty() {
            size - 1
        } else {
            end.parse::<u64>()
                .map_err(|_| ApiError::Invalid)?
                .min(size - 1)
        };
        if end < start {
            return Ok(None);
        }
        (start, end)
    };
    Ok(Some(ByteRange {
        start,
        end,
        partial: true,
    }))
}

fn range_error(size: u64) -> Response {
    let mut response = (
        StatusCode::RANGE_NOT_SATISFIABLE,
        Json(serde_json::json!({"code":"range_not_satisfiable"})),
    )
        .into_response();
    if let Ok(value) = HeaderValue::from_str(&format!("bytes */{size}")) {
        response.headers_mut().insert(CONTENT_RANGE, value);
    }
    response
}

fn spawn_reader(
    store: px_console_store::RecordingCacheStore,
    cache: CacheRuntime,
    mut lease: CacheReadLease,
    reader: BlobReader,
    start: u64,
    length: u64,
    cancellation: tokio_util::sync::CancellationToken,
) -> mpsc::Receiver<Result<Bytes, std::io::Error>> {
    let (sender, receiver) = mpsc::channel(2);
    tokio::spawn(async move {
        let mut reader = match tokio::task::spawn_blocking(move || {
            let mut reader = reader;
            let result = reader.seek(SeekFrom::Start(start));
            (reader, result)
        })
        .await
        {
            Ok((reader, Ok(_))) => reader,
            _ => {
                let _ = sender
                    .send(Err(std::io::Error::other("recording seek failed")))
                    .await;
                let _ = store.close_read(&cache, &lease).await;
                return;
            }
        };
        let mut remaining = length;
        let mut lease_deadline =
            Instant::now() + Duration::from_millis(u64::from(lease.valid_for_ms()));
        while remaining > 0 && !cancellation.is_cancelled() {
            if lease_deadline.saturating_duration_since(Instant::now()) <= RENEW_BEFORE {
                match store.renew_read(&cache, &lease, &reader).await {
                    Ok(renewed) => {
                        lease = renewed;
                        lease_deadline =
                            Instant::now() + Duration::from_millis(u64::from(lease.valid_for_ms()));
                    }
                    Err(_) => break,
                }
            }
            let count = remaining.min(CHUNK_BYTES) as usize;
            let read = tokio::task::spawn_blocking(move || {
                let mut reader = reader;
                let mut bytes = vec![0_u8; count];
                let result = reader.read_exact(&mut bytes);
                (reader, result, bytes)
            })
            .await;
            let (returned_reader, result, bytes) = match read {
                Ok(result) => result,
                Err(_) => {
                    let _ = sender
                        .send(Err(std::io::Error::other("recording read failed")))
                        .await;
                    break;
                }
            };
            reader = returned_reader;
            if result.is_err() {
                let _ = sender
                    .send(Err(std::io::Error::other("recording read failed")))
                    .await;
                break;
            }
            let bytes = Bytes::from(bytes);
            loop {
                tokio::select! {
                    _ = cancellation.cancelled() => break,
                    permit = tokio::time::timeout(SEND_WAIT, sender.reserve()) => match permit {
                        Ok(Ok(permit)) => {
                            permit.send(Ok(bytes));
                            remaining -= count as u64;
                            break;
                        }
                        Ok(Err(_)) => {
                            remaining = 0;
                            break;
                        }
                        Err(_) => match store.renew_read(&cache, &lease, &reader).await {
                            Ok(renewed) => {
                                lease = renewed;
                                lease_deadline = Instant::now()
                                    + Duration::from_millis(u64::from(lease.valid_for_ms()));
                            }
                            Err(_) => {
                                remaining = 0;
                                break;
                            }
                        },
                    },
                }
                if cancellation.is_cancelled() {
                    remaining = 0;
                    break;
                }
            }
        }
        let _ = store.close_read(&cache, &lease).await;
    });
    receiver
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ranges_are_single_bounded_and_deterministic() {
        let mut headers = HeaderMap::new();
        assert_eq!(
            parse_range(&headers, 100).unwrap(),
            Some(ByteRange {
                start: 0,
                end: 99,
                partial: false,
            })
        );
        headers.insert(RANGE, HeaderValue::from_static("bytes=10-19"));
        assert_eq!(
            parse_range(&headers, 100).unwrap(),
            Some(ByteRange {
                start: 10,
                end: 19,
                partial: true,
            })
        );
        headers.insert(RANGE, HeaderValue::from_static("bytes=-7"));
        assert_eq!(parse_range(&headers, 100).unwrap().unwrap().start, 93);
        headers.insert(RANGE, HeaderValue::from_static("bytes=95-999"));
        assert_eq!(parse_range(&headers, 100).unwrap().unwrap().end, 99);
        for rejected in ["bytes=100-", "bytes=20-10", "bytes=0-1,4-5", "bytes=-0"] {
            headers.insert(RANGE, HeaderValue::from_str(rejected).unwrap());
            assert_eq!(parse_range(&headers, 100).unwrap(), None);
        }
    }
}
