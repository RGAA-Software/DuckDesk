SELECT n.id,n.generation,n.control_epoch AS "control_epoch!",n.endpoint_revision
FROM pixels.nodes n JOIN pixels.devices d ON d.id=n.device_id
WHERE n.id=$1 AND n.connection_hash=$2 AND n.generation=$3 AND n.control_epoch=$4
AND $4=(SELECT epoch FROM pixels.control_runtime) AND n.last_seen>clock_timestamp()-interval '30 seconds'
AND n.report_sequence>0 AND NOT n.disabled AND n.deleted_at IS NULL AND NOT d.disabled AND d.deleted_at IS NULL
FOR UPDATE OF n
