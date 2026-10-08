UPDATE pixels.application_deployments d SET gpu_key=$2,gpu_memory_bytes=$3,gpu_compute_per_mille=$4,
gpu_encoder_per_mille=$5,gpu_memory_reserve_bytes=$6,gpu_compute_limit_per_mille=$7,gpu_encoder_limit_per_mille=$8,capacity=$9,disabled=$10,
revision=d.revision+1,application_revision=a.revision,observed_state='pending',observed_reason=NULL,
observed_generation=NULL,observed_epoch=NULL,observed_endpoint_revision=NULL,observed_sequence=0,observed_at=NULL
FROM pixels.applications a,pixels.nodes n,pixels.devices v
WHERE d.id=$1 AND a.id=d.application_id AND a.kind=$11 AND a.deleted_at IS NULL
AND n.id=d.node_id AND n.deleted_at IS NULL AND v.id=n.device_id AND v.deleted_at IS NULL
RETURNING d.id,d.application_id,d.node_id,d.kind,d.gpu_key,d.gpu_memory_bytes,d.gpu_compute_per_mille,d.gpu_encoder_per_mille,d.gpu_memory_reserve_bytes,d.gpu_compute_limit_per_mille,d.gpu_encoder_limit_per_mille,d.capacity,d.disabled,d.revision,d.application_revision,d.observed_state,d.observed_reason,d.observed_generation,d.observed_epoch,d.observed_endpoint_revision,d.observed_sequence,d.observed_at
