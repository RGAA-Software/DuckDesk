SELECT id,instance_id,node_id,node_generation,control_epoch,instance_revision,kind,state,attempts,lease_id,lease_until,deadline,outcome,completed_lease_id,
(deadline<=clock_timestamp()) AS "expired!"
FROM pixels.instance_commands WHERE node_id=$1 AND node_generation=$2 AND control_epoch=$3
AND (state='pending' OR (state='claimed' AND lease_until<=clock_timestamp()))
ORDER BY CASE WHEN kind='stop' THEN 0 ELSE 1 END,created_at,id LIMIT 1 FOR UPDATE
