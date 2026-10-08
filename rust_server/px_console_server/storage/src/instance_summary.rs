use crate::{control, InstanceStore, StoreError, TokenDigest};
use uuid::Uuid;

#[derive(Debug, serde::Serialize, sqlx::FromRow)]
pub struct InstanceSummary {
    pub application_id: Uuid,
    pub node_id: Uuid,
    pub state: String,
    pub count: i64,
}

impl InstanceStore {
    pub async fn managed_summary(
        &self,
        administrator: &TokenDigest,
    ) -> Result<Vec<InstanceSummary>, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, administrator, false).await?;
        let summary = sqlx::query_as::<_, InstanceSummary>(include_str!(
            "../queries/managed_instance_summary.sql"
        ))
        .fetch_all(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(summary)
    }
}
