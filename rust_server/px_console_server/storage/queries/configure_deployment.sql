UPDATE pixels.application_deployments d SET install_root=$2,gpu_key=$3,capacity=$4,disabled=$5,
revision=d.revision+1,application_revision=a.revision,observed_state='pending',observed_reason=NULL,
observed_generation=NULL,observed_epoch=NULL,observed_endpoint_revision=NULL,observed_sequence=0,observed_at=NULL
FROM pixels.applications a,pixels.nodes n,pixels.devices v
WHERE d.id=$1 AND a.id=d.application_id AND a.kind=$6 AND a.deleted_at IS NULL
AND n.id=d.node_id AND n.deleted_at IS NULL AND v.id=n.device_id AND v.deleted_at IS NULL
RETURNING d.id,d.application_id,d.node_id,d.kind,d.install_root,d.gpu_key,d.capacity,d.disabled,d.revision,d.application_revision,d.observed_state,d.observed_reason,d.observed_generation,d.observed_epoch,d.observed_endpoint_revision,d.observed_sequence,d.observed_at
