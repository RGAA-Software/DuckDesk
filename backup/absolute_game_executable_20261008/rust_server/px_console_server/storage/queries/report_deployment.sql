UPDATE pixels.application_deployments d SET observed_state=$9,observed_reason=$10,
observed_generation=n.generation,observed_epoch=n.control_epoch,observed_endpoint_revision=n.endpoint_revision,
observed_sequence=$8,observed_at=clock_timestamp()
FROM pixels.nodes n,pixels.applications a,pixels.devices v
WHERE d.id=$1 AND d.node_id=n.id AND a.id=d.application_id AND v.id=n.device_id
AND n.connection_hash=$2 AND n.generation=$3 AND n.control_epoch=$4 AND $4=(SELECT epoch FROM pixels.control_runtime)
AND n.endpoint_revision=$5 AND n.last_seen>clock_timestamp()-interval '30 seconds' AND n.report_sequence>0
AND NOT n.disabled AND n.deleted_at IS NULL AND NOT v.disabled AND v.deleted_at IS NULL
AND d.revision=$6 AND d.application_revision=$7 AND a.revision=$7 AND a.deleted_at IS NULL
AND (d.observed_generation IS DISTINCT FROM $3 OR d.observed_epoch IS DISTINCT FROM $4 OR d.observed_sequence<$8)
RETURNING d.id,d.application_id,d.node_id,d.kind,d.install_root,d.gpu_key,d.gpu_memory_bytes,d.gpu_compute_per_mille,d.gpu_encoder_per_mille,d.gpu_memory_reserve_bytes,d.gpu_compute_limit_per_mille,d.gpu_encoder_limit_per_mille,d.capacity,d.disabled,d.revision,d.application_revision,d.observed_state,d.observed_reason,d.observed_generation,d.observed_epoch,d.observed_endpoint_revision,d.observed_sequence,d.observed_at
