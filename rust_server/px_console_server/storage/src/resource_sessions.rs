use crate::{
    control,
    instances::AdmissionSubject,
    session_model::{SessionEndpoint, SessionRow},
    ClientType, InstanceStore, OpenResourceSession, ResourceCredential, ResourceDescriptor,
    ResourceSession, RuntimeEntitlement, SessionTarget, StoreError, TokenDigest,
};
use sqlx::PgPool;
use uuid::Uuid;

#[derive(Clone)]
pub struct ResourceSessionStore {
    pub(crate) pool: PgPool,
}
impl ResourceSessionStore {
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

    #[cfg(feature = "pg-integration")]
    pub async fn open(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        request: &OpenResourceSession,
    ) -> Result<ResourceSession, StoreError> {
        self.open_with_entitlement(
            credential,
            client,
            request,
            RuntimeEntitlement::unrestricted_for_integration(),
        )
        .await
    }

    pub async fn open_with_entitlement(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        request: &OpenResourceSession,
        entitlement: RuntimeEntitlement,
    ) -> Result<ResourceSession, StoreError> {
        let hash = request.digest(client)?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let (user, guest) = subject.owner.columns();
        if let Some(existing) = sqlx::query_file_as!(
            SessionRow,
            "queries/find_resource_session_request.sql",
            request.request_id,
            user,
            guest
        )
        .fetch_optional(&mut *tx)
        .await?
        {
            Self::same_origin(&existing, &subject, client)?;
            if existing.request_hash.as_slice() != hash {
                return Err(StoreError::Rejected);
            }
            let result = existing.view()?;
            tx.commit().await?;
            return Ok(result);
        }
        Self::enforce_target_entitlement(&mut tx, request.target, entitlement).await?;
        sqlx::query("SELECT pg_advisory_xact_lock(5788347791197331458)")
            .execute(&mut *tx)
            .await?;
        let active_sessions: i64 = sqlx::query_scalar(
            "SELECT count(*) FROM pixels.resource_sessions WHERE closed_at IS NULL",
        )
        .fetch_one(&mut *tx)
        .await?;
        if active_sessions >= i64::from(entitlement.max_sessions) {
            return Err(StoreError::LicenseRestriction);
        }
        let endpoint = Self::endpoint(
            &mut tx,
            request.target,
            &subject,
            client,
            request.access.name(),
        )
        .await?;
        let (kind, device, app, instance) = match request.target {
            SessionTarget::Desktop { device_id } => ("desktop", Some(device_id), None, None),
            SessionTarget::CloudApplication {
                application_id,
                instance_id,
            } => (
                "cloud_application",
                None,
                Some(application_id),
                Some(instance_id),
            ),
        };
        if !sqlx::query_file_scalar!(
            "queries/resource_session_capacity.sql",
            endpoint.node_id,
            device,
            instance,
            request.access.name()
        )
        .fetch_one(&mut *tx)
        .await?
        {
            return Err(StoreError::NoCapacity);
        }
        let row = sqlx::query_file_as!(
            SessionRow,
            "queries/create_resource_session.sql",
            Uuid::new_v4(),
            kind,
            device,
            app,
            instance,
            endpoint.node_id,
            user,
            guest,
            subject.login_session,
            subject.revision,
            client.name(),
            request.access.name(),
            request.request_id,
            hash.as_slice(),
            endpoint.generation,
            endpoint.control_epoch,
            endpoint.endpoint_revision
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row, "created").await?;
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn get(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        id: Uuid,
    ) -> Result<ResourceSession, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let row = Self::lock(&mut tx, id).await?;
        // History is visible to the same principal, but a different login cannot renew its grant.
        if row.view()?.owner != subject.owner || row.client_type != client.name() {
            return Err(StoreError::Rejected);
        }
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    /// Composition root generates the random token and supplies only its digest here.
    /// Caller must not return any grant to the frontend until this transaction commits.
    #[cfg(feature = "pg-integration")]
    pub async fn descriptor(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        id: Uuid,
        expected_revision: i64,
        token: &TokenDigest,
    ) -> Result<ResourceDescriptor, StoreError> {
        self.descriptor_with_entitlement(
            credential,
            client,
            id,
            expected_revision,
            token,
            RuntimeEntitlement::unrestricted_for_integration(),
        )
        .await
    }

    pub async fn descriptor_with_entitlement(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        id: Uuid,
        expected_revision: i64,
        token: &TokenDigest,
        entitlement: RuntimeEntitlement,
    ) -> Result<ResourceDescriptor, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let mut row = Self::lock(&mut tx, id).await?;
        Self::same_origin(&row, &subject, client)?;
        if row.revision != expected_revision
            || !matches!(row.state.as_str(), "pending" | "connected")
        {
            return Err(StoreError::Rejected);
        }
        Self::enforce_target_entitlement(&mut tx, row.view()?.target, entitlement).await?;
        let endpoint = Self::live_endpoint(&mut tx, &row).await?;
        let issued = sqlx::query_file!(
            "queries/issue_resource_descriptor.sql",
            id,
            token.0.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        row.revision = issued.revision;
        Self::event(&mut tx, &row, "descriptor").await?;
        let result = ResourceDescriptor {
            session: row.view()?,
            node_id: endpoint.node_id,
            node_generation: endpoint.generation,
            control_epoch: endpoint.control_epoch,
            endpoint_revision: endpoint.endpoint_revision,
            host: endpoint.host,
            port: endpoint.port.try_into().map_err(|_| StoreError::Rejected)?,
            transport: endpoint.transport,
            expires_at: issued.expires_at,
        };
        tx.commit().await?;
        Ok(result)
    }

    async fn enforce_target_entitlement(
        connection: &mut sqlx::PgConnection,
        target: SessionTarget,
        entitlement: RuntimeEntitlement,
    ) -> Result<(), StoreError> {
        let permitted = match target {
            SessionTarget::Desktop { .. } => entitlement.desktop,
            SessionTarget::CloudApplication {
                application_id,
                instance_id,
            } => {
                let kind = sqlx::query_scalar::<_, String>(
                    "SELECT kind FROM pixels.instances WHERE id=$1 AND application_id=$2 AND ended_at IS NULL",
                )
                .bind(instance_id)
                .bind(application_id)
                .fetch_optional(&mut *connection)
                .await?
                .ok_or(StoreError::Rejected)?;
                entitlement.permits_application_kind(&kind)
            }
        };
        if !permitted {
            return Err(StoreError::LicenseRestriction);
        }
        Ok(())
    }
    pub async fn request_close(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        id: Uuid,
        expected_revision: i64,
    ) -> Result<ResourceSession, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let row = Self::lock(&mut tx, id).await?;
        Self::same_origin(&row, &subject, client)?;
        if row.revision != expected_revision {
            return Err(StoreError::Rejected);
        }
        let result = if matches!(row.state.as_str(), "closed" | "closing") {
            row.view()?
        } else {
            Self::change(&mut tx, &row, "closing").await?.view()?
        };
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ResourceSession>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let rows = sqlx::query_file_as!(
            SessionRow,
            "queries/managed_resource_sessions.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let result = rows
            .iter()
            .map(SessionRow::view)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }
    pub(crate) fn same_origin(
        row: &SessionRow,
        subject: &AdmissionSubject,
        client: ClientType,
    ) -> Result<(), StoreError> {
        if row.view()?.owner != subject.owner
            || row.login_session_id != subject.login_session
            || row.owner_revision != subject.revision
            || row.client_type != client.name()
        {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    pub(crate) async fn live_endpoint(
        connection: &mut sqlx::PgConnection,
        row: &SessionRow,
    ) -> Result<SessionEndpoint, StoreError> {
        if !sqlx::query_file_scalar!("queries/resource_session_origin_authorized.sql", row.id)
            .fetch_one(&mut *connection)
            .await?
        {
            return Err(StoreError::Rejected);
        }
        let client = match row.client_type.as_str() {
            "panel" => ClientType::Panel,
            "android" => ClientType::Android,
            "user_web" => ClientType::UserWeb,
            _ => return Err(StoreError::Rejected),
        };
        let view = row.view()?;
        let subject = AdmissionSubject {
            owner: view.owner,
            login_session: row.login_session_id,
            revision: row.owner_revision,
        };
        let endpoint =
            Self::endpoint(connection, view.target, &subject, client, &row.access_role).await?;
        if endpoint.node_id != row.node_id
            || endpoint.generation != row.node_generation
            || endpoint.control_epoch != row.control_epoch
            || endpoint.endpoint_revision != row.endpoint_revision
        {
            return Err(StoreError::Rejected);
        }
        Ok(endpoint)
    }
}
