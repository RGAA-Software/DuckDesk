INSERT INTO pixels.application_deployments AS d(id,application_id,node_id,kind,install_root,gpu_key,capacity,disabled,application_revision)
SELECT $1,a.id,n.id,$4,$5,$6,$7,$8,a.revision FROM pixels.applications a,pixels.nodes n,pixels.devices v
WHERE a.id=$2 AND n.id=$3 AND a.kind=$4 AND a.deleted_at IS NULL AND n.deleted_at IS NULL
AND v.id=n.device_id AND v.deleted_at IS NULL
RETURNING d.id,d.application_id,d.node_id,d.kind,d.install_root,d.gpu_key,d.capacity,d.disabled,d.revision,d.application_revision,d.observed_state,d.observed_reason,d.observed_generation,d.observed_epoch,d.observed_endpoint_revision,d.observed_sequence,d.observed_at
