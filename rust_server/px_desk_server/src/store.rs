use crate::{
    error::ApiError,
    model::{Feedback, Kind, Mark, Page, Release, ReleaseInput, ReleaseQuery, Submission},
};
use sha2::{Digest, Sha256};
use sqlx::PgPool;
use uuid::Uuid;

pub async fn submit(pool: &PgPool, kind: Kind, input: Submission) -> Result<Uuid, ApiError> {
    input.validate(kind)?;
    let body = serde_json::to_vec(&input).map_err(|_| ApiError::Internal)?;
    let digest = Sha256::digest(&body);
    // ON CONFLICT waits for any concurrent insert before the subsequent statement reads it.
    // Row contents are immutable: runtime may only update processed/revision/updated_at.
    sqlx::query_file!(
        "queries/insert_feedback.sql",
        input.request_id,
        kind.name(),
        &input.title,
        &input.your_name,
        &input.description,
        &input.email,
        &input.wechat,
        &input.qq,
        input.consult_type.as_deref(),
        input.version.as_deref(),
        input.os.as_deref(),
        &digest[..]
    )
    .execute(pool)
    .await?;
    let stored = sqlx::query_file!("queries/feedback_fingerprint.sql", input.request_id)
        .fetch_one(pool)
        .await?;
    if stored.kind != kind.name() || stored.body_sha256 != digest[..] {
        return Err(ApiError::Conflict);
    }
    Ok(input.request_id)
}
pub async fn list(pool: &PgPool, kind: Kind, page: Page) -> Result<Vec<Feedback>, ApiError> {
    page.validate()?;
    Ok(sqlx::query_file_as!(
        Feedback,
        "queries/list_feedback.sql",
        kind.name(),
        page.processed,
        i64::from(page.page_size),
        i64::from(page.page - 1) * i64::from(page.page_size)
    )
    .fetch_all(pool)
    .await?)
}
pub async fn mark(pool: &PgPool, kind: Kind, id: Uuid, input: Mark) -> Result<Feedback, ApiError> {
    if input.expected_revision < 1 {
        return Err(ApiError::Invalid);
    }
    // A mismatching revision OR unknown ID is a conflict: no existence disclosure or second-query race.
    sqlx::query_file_as!(
        Feedback,
        "queries/mark_feedback.sql",
        input.processed,
        id,
        kind.name(),
        input.expected_revision
    )
    .fetch_optional(pool)
    .await?
    .ok_or(ApiError::Conflict)
}
pub async fn publish(pool: &PgPool, input: ReleaseInput) -> Result<Release, ApiError> {
    input
        .validate_immutable_target_name()
        .map_err(|_| ApiError::Invalid)?;
    Ok(sqlx::query_file_as!(
        Release,
        "queries/publish_version.sql",
        Uuid::new_v4(),
        input.target.product.name(),
        input.target.distribution.name(),
        input.target.release_namespace,
        input.target.oem_id,
        input.target.channel.name(),
        input.build_number,
        input.version,
        input.metadata_base_url,
        input.targets_base_url,
        input.target_name,
        input.sha256,
        input.platform_signer_sha256,
        input.target.os.name(),
        input.target.architecture.name(),
        input.size_bytes
    )
    .fetch_one(pool)
    .await?)
}
pub async fn latest(pool: &PgPool, input: ReleaseQuery) -> Result<Release, ApiError> {
    input.validate().map_err(|_| ApiError::Invalid)?;
    sqlx::query_file_as!(
        Release,
        "queries/latest_version.sql",
        input.product.name(),
        input.distribution.name(),
        input.release_namespace,
        input.oem_id,
        input.channel.name(),
        input.os.name(),
        input.architecture.name()
    )
    .fetch_optional(pool)
    .await?
    .ok_or(ApiError::NotFound)
}
