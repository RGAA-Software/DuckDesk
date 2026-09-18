use crate::{
    control, instance_model::InstanceRow, ApplicationInstance, ApplicationStore, ClientType,
    GuestStore, ResourceCredential, ResourceOwner, RuntimeEpoch, StartApplication, StoreError,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct InstanceStore {
    pub(crate) pool: PgPool,
}
pub(crate) struct AdmissionSubject {
    pub owner: ResourceOwner,
    pub login_session: Option<Uuid>,
    pub revision: i64,
}
impl InstanceStore {
    /// Isolated multi-process database acceptance only. Neither this adapter nor its
    /// test binary is compiled into the normal product feature set.
    #[cfg(feature = "pg-integration")]
    pub async fn reserve_in_isolated_test_runtime(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        request: &StartApplication,
    ) -> Result<ApplicationInstance, StoreError> {
        if std::env::var("PIXELS_PG_ISOLATED_TEST").as_deref() != Ok("1") {
            return Err(StoreError::Rejected);
        }
        let epoch: i64 = sqlx::query_scalar("SELECT epoch FROM pixels.control_runtime")
            .fetch_one(&self.pool)
            .await?;
        self.reserve(credential, client, RuntimeEpoch(epoch), request)
            .await
    }
    #[cfg(feature = "pg-integration")]
    pub async fn connect(
        config: &px_pg::DatabaseConfig,
        deployment: Uuid,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
        })
    }
    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }
    pub async fn reserve(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        epoch: RuntimeEpoch,
        request: &StartApplication,
    ) -> Result<ApplicationInstance, StoreError> {
        let hash = request.digest(client)?;
        let mut tx = self.pool.begin().await?;
        // Same bounded global gate as maintenance/ACL changes. SQL constraints remain
        // independent protection; no in-memory capacity counter is authoritative.
        control::write_gate(&mut tx).await?;
        let subject = Self::authorize(&mut tx, credential, client).await?;
        let owner = subject.owner;
        Self::visible(&mut tx, owner, request.application_id).await?;
        let (user, guest) = owner.columns();
        if let Some(existing) = sqlx::query_file_as!(
            InstanceRow,
            "queries/find_instance_request.sql",
            request.request_id,
            user,
            guest
        )
        .fetch_optional(&mut *tx)
        .await?
        {
            if existing.request_hash.as_slice() != hash {
                return Err(StoreError::Rejected);
            }
            let result = existing.view()?;
            tx.commit().await?;
            return Ok(result);
        }
        let instance = sqlx::query_file_as!(
            InstanceRow,
            "queries/reserve_instance.sql",
            Uuid::new_v4(),
            request.application_id,
            request.deployment_id,
            user,
            guest,
            client.name(),
            request.request_id,
            hash.as_slice(),
            epoch.0,
            Uuid::new_v4(),
            subject.login_session,
            subject.revision
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::NoCapacity)?;
        Self::command(&mut tx, &instance, "start").await?;
        Self::event(&mut tx, &instance, "reserved").await?;
        let result = instance.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn get(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        id: Uuid,
    ) -> Result<ApplicationInstance, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let owner = Self::authorize(&mut tx, credential, client).await?.owner;
        let (user, guest) = owner.columns();
        let instance =
            sqlx::query_file_as!(InstanceRow, "queries/owned_instance.sql", id, user, guest)
                .fetch_optional(&mut *tx)
                .await?
                .ok_or(StoreError::Rejected)?;
        Self::visible(&mut tx, owner, instance.application_id).await?;
        let result = instance.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_owned(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ApplicationInstance>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let owner = Self::authorize(&mut tx, credential, client).await?.owner;
        let (user, guest) = owner.columns();
        let rows = sqlx::query_file_as!(
            InstanceRow,
            "queries/owned_instances.sql",
            user,
            guest,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let result = rows
            .iter()
            .map(InstanceRow::view)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }
    pub(crate) async fn authorize(
        connection: &mut PgConnection,
        credential: ResourceCredential<'_>,
        client: ClientType,
    ) -> Result<AdmissionSubject, StoreError> {
        match credential {
            ResourceCredential::User(token) => {
                let subject = sqlx::query_file!(
                    "queries/authorize_instance_user.sql",
                    token.0.as_slice(),
                    client.name()
                )
                .fetch_optional(connection)
                .await?
                .ok_or(StoreError::Rejected)?;
                Ok(AdmissionSubject {
                    owner: ResourceOwner::User {
                        user_id: subject.user_id,
                    },
                    login_session: Some(subject.session_id),
                    revision: subject.authorization_revision,
                })
            }
            ResourceCredential::Guest(token) => {
                let subject = GuestStore::authorize(connection, token, client).await?;
                Ok(AdmissionSubject {
                    owner: ResourceOwner::Guest {
                        guest_id: subject.id,
                    },
                    login_session: None,
                    revision: subject.revision,
                })
            }
        }
    }
    pub(crate) async fn visible(
        connection: &mut PgConnection,
        owner: ResourceOwner,
        application: Uuid,
    ) -> Result<(), StoreError> {
        let rows =
            ApplicationStore::visible(connection, owner.columns().0, None, 1, Some(application))
                .await?;
        if rows.is_empty() {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    pub(crate) async fn command(
        connection: &mut PgConnection,
        instance: &InstanceRow,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/create_instance_command.sql",
            Uuid::new_v4(),
            instance.id,
            instance.node_id,
            instance.node_generation,
            instance.control_epoch,
            instance.revision,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
    pub(crate) async fn event(
        connection: &mut PgConnection,
        instance: &InstanceRow,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/instance_event.sql",
            Uuid::new_v4(),
            instance.id,
            instance.revision,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
