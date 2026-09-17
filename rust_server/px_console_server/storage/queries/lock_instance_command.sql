SELECT id,instance_id,node_id,node_generation,control_epoch,instance_revision,kind,state,attempts,lease_id,lease_until,deadline,outcome,completed_lease_id,
(deadline<=clock_timestamp() OR COALESCE(lease_until<=clock_timestamp(),false)) AS "expired!"
FROM pixels.instance_commands WHERE id=$1 FOR UPDATE
