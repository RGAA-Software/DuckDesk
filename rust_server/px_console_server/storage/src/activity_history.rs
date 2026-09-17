use crate::{
    activity_model::{page, ChannelRow, VisitRow},
    control, ActivityStore, ChannelRecord, ClientType, InstanceStore, ResourceCredential,
    StoreError, TokenDigest, VisitRecord,
};
use uuid::Uuid;
impl ActivityStore {
    pub async fn channels_managed(
        &self,
        token: &TokenDigest,
        session: Option<Uuid>,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ChannelRecord>, StoreError> {
        page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let rows = sqlx::query_file_as!(
            ChannelRow,
            "queries/managed_connection_observations.sql",
            after,
            session,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(rows.into_iter().map(ChannelRow::view).collect())
    }
    pub async fn channels_owned(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        session: Option<Uuid>,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ChannelRecord>, StoreError> {
        page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let (user, guest) = subject.owner.columns();
        let rows = sqlx::query_file_as!(
            ChannelRow,
            "queries/owned_connection_observations.sql",
            user,
            guest,
            client.name(),
            after,
            session,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(rows.into_iter().map(ChannelRow::view).collect())
    }
    pub async fn visits_managed(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<VisitRecord>, StoreError> {
        page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let rows = sqlx::query_file_as!(
            VisitRow,
            "queries/managed_visits.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let result = rows
            .into_iter()
            .map(VisitRow::view)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn visits_owned(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<VisitRecord>, StoreError> {
        page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let (user, guest) = subject.owner.columns();
        let rows = sqlx::query_file_as!(
            VisitRow,
            "queries/owned_visits.sql",
            user,
            guest,
            client.name(),
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let result = rows
            .into_iter()
            .map(VisitRow::view)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }
}
