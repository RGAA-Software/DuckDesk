SELECT n.id FROM pixels.nodes n
WHERE n.id=$1 AND n.connection_hash=$2 AND n.generation=$3 AND n.control_epoch=$4
 AND $4=(SELECT epoch FROM pixels.control_runtime)
 AND NOT n.disabled AND n.deleted_at IS NULL
 AND EXISTS(SELECT 1 FROM pixels.devices d WHERE d.id=n.device_id AND NOT d.disabled AND d.deleted_at IS NULL)
FOR UPDATE OF n
