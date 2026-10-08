use crate::{control, instance_state, node_lifecycle, InstanceStore, NodeConnection, StoreError};
use uuid::Uuid;

impl InstanceStore {
    /// Exact launch identity, not a PID/path/port search. A repeated receipt must
    /// never change a terminal instance or release a replacement launch's port.
    pub async fn report_runtime_exit(
        &self,
        connection: &NodeConnection,
        instance_id: Uuid,
        launch_id: Uuid,
        port: u16,
        failed: bool,
    ) -> Result<(), StoreError> {
        if instance_id.is_nil() || launch_id.is_nil() || port == 0 {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let authority = node_lifecycle::authorize(&mut transaction, connection).await?;
        let instance = instance_state::lock(&mut transaction, instance_id).await?;
        if instance.node_id != authority.id
            || instance.launch_id != launch_id
            || instance.port != i32::from(port)
            || instance.node_generation != authority.generation
            || instance.control_epoch != authority.control_epoch
        {
            return Err(StoreError::Rejected);
        }
        if instance.ended_at.is_none() {
            instance_state::cancel(&mut transaction, instance_id).await?;
            let transition = if failed {
                instance_state::Transition::Failed
            } else {
                instance_state::Transition::Stopped
            };
            instance_state::change(&mut transaction, &instance, Some(&authority), transition)
                .await?;
        }
        transaction.commit().await?;
        Ok(())
    }
}
