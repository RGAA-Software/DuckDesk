use crate::{ClientType, StoreError, TokenDigest};
use sqlx::PgConnection;
use uuid::Uuid;

// Used inside an admission transaction, after the shared authorization gate. The caller
// must retain that gate through its side effects; a completed read is not an access ticket.
pub(crate) async fn resource_user(
    connection: &mut PgConnection,
    token: &TokenDigest,
    client: ClientType,
) -> Result<Uuid, StoreError> {
    sqlx::query_file_scalar!(
        "queries/authorize_resource_user.sql",
        token.0.as_slice(),
        client.name()
    )
    .fetch_optional(connection)
    .await?
    .ok_or(StoreError::Rejected)
}
