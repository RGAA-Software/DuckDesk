SELECT i.application_id,i.deployment_id,i.node_id
FROM pixels.instance_commands c JOIN pixels.instances i ON i.id=c.instance_id
WHERE c.id=$1 AND c.lease_id=$2 AND c.node_id=$3 AND c.node_generation=$4 AND c.control_epoch=$5
AND c.kind='start' AND c.state='claimed' AND c.deadline>clock_timestamp() AND c.lease_until>clock_timestamp()
AND c.instance_revision=i.revision AND i.state='starting' AND i.desired_state='running' AND i.ended_at IS NULL AND i.kind='rdp'
