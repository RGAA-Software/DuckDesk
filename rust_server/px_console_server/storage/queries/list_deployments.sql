SELECT d.id,d.application_id,d.node_id,d.kind,d.gpu_key,d.capacity,d.disabled,d.revision,d.application_revision,d.observed_state,d.observed_reason,d.observed_generation,d.observed_epoch,d.observed_endpoint_revision,d.observed_sequence,d.observed_at FROM pixels.application_deployments d
JOIN pixels.applications a ON a.id=d.application_id
WHERE a.deleted_at IS NULL AND ($1::uuid IS NULL OR d.id>$1) ORDER BY d.id LIMIT $2
