SELECT i.id,i.application_id,i.node_id,i.owner_user,i.owner_guest,i.client_type,i.request_hash,i.state,i.revision,i.node_generation,i.control_epoch,i.created_at,i.ended_at,i.deployment_id,i.launch_id,i.desired_state,i.application_revision,i.deployment_revision,i.endpoint_revision,i.port
FROM pixels.instances i
WHERE (i.owner_user=$1 OR i.owner_guest=$2)
  AND ($3::uuid IS NULL OR i.id>$3)
ORDER BY i.id
LIMIT $4
