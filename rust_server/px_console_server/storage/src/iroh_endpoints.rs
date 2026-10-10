use px_node_protocol::{IrohConnectionDescription, RenderIrohEndpoint};
use sqlx::PgConnection;
use uuid::Uuid;

use crate::StoreError;

pub(crate) async fn replace(
    connection: &mut PgConnection,
    node_id: Uuid,
    generation: i64,
    control_epoch: i64,
    endpoints: &[RenderIrohEndpoint],
) -> Result<(), StoreError> {
    sqlx::query("DELETE FROM pixels.render_iroh_endpoints WHERE node_id=$1")
        .bind(node_id)
        .execute(&mut *connection)
        .await?;
    for endpoint in endpoints {
        // Application endpoints must belong to the exact active launch, not a reused port.
        if let (Some(instance_id), Some(launch_id)) = (endpoint.instance_id, endpoint.launch_id) {
            let current: bool = sqlx::query_scalar(
                "SELECT EXISTS(SELECT 1 FROM pixels.instances WHERE id=$1 AND launch_id=$2 AND node_id=$3 AND port=$4 AND ended_at IS NULL)"
            ).bind(instance_id).bind(launch_id).bind(node_id).bind(i32::from(endpoint.port))
                .fetch_one(&mut *connection).await?;
            if !current {
                continue;
            }
        }
        let description =
            serde_json::to_string(&endpoint.description).map_err(|_| StoreError::InvalidInput)?;
        sqlx::query(
            "INSERT INTO pixels.render_iroh_endpoints(node_id,node_generation,control_epoch,port,instance_id,launch_id,description) \
             VALUES($1,$2,$3,$4,$5,$6,$7)"
        ).bind(node_id).bind(generation).bind(control_epoch).bind(i32::from(endpoint.port))
            .bind(endpoint.instance_id).bind(endpoint.launch_id).bind(description)
            .execute(&mut *connection).await?;
    }
    Ok(())
}

pub(crate) async fn resolve(
    connection: &mut PgConnection,
    node_id: Uuid,
    generation: i64,
    control_epoch: i64,
    port: i32,
    instance_id: Option<Uuid>,
) -> Result<Option<IrohConnectionDescription>, StoreError> {
    let description: Option<String> = sqlx::query_scalar(
        "SELECT endpoint.description FROM pixels.render_iroh_endpoints endpoint \
         WHERE endpoint.node_id=$1 AND endpoint.node_generation=$2 AND endpoint.control_epoch=$3 AND endpoint.port=$4 \
         AND endpoint.instance_id IS NOT DISTINCT FROM $5 \
         AND (endpoint.instance_id IS NULL OR EXISTS(SELECT 1 FROM pixels.instances runtime WHERE runtime.id=endpoint.instance_id \
              AND runtime.launch_id=endpoint.launch_id AND runtime.ended_at IS NULL))"
    ).bind(node_id).bind(generation).bind(control_epoch).bind(port).bind(instance_id)
        .fetch_optional(connection).await?;
    description
        .map(|encoded| serde_json::from_str(&encoded).map_err(|_| StoreError::Rejected))
        .transpose()
}
