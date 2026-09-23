use crate::{RelayBinding, StoreError};

const RELAY_SELECTION_LOCK: i64 = 5_788_347_791_197_331_459;

pub(crate) async fn select(
    connection: &mut sqlx::PgConnection,
    control_epoch: i64,
) -> Result<Option<RelayBinding>, StoreError> {
    sqlx::query("SELECT pg_advisory_xact_lock($1)")
        .bind(RELAY_SELECTION_LOCK)
        .execute(&mut *connection)
        .await?;
    Ok(sqlx::query_file_as!(
        RelayBinding,
        "queries/select_relay_for_session.sql",
        control_epoch
    )
    .fetch_optional(connection)
    .await?)
}
