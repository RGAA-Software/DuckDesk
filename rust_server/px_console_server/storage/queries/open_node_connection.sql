UPDATE pixels.nodes n SET connection_hash=$2,generation=n.generation+1,control_epoch=$3,state='reconciling',reconciliation_id=NULL,reconciliation_deadline=NULL,report_sequence=0,last_seen=clock_timestamp()
WHERE n.credential_hash=$1 AND NOT n.disabled AND n.deleted_at IS NULL AND $3=(SELECT epoch FROM pixels.control_runtime)
AND EXISTS(SELECT 1 FROM pixels.devices d WHERE d.id=n.device_id AND NOT d.disabled AND d.deleted_at IS NULL)
RETURNING id,device_id,product,revision,generation,control_epoch,state,draining,disabled,max_instances,report_sequence,last_seen,product_version_code,public_host,desktop_port,application_port_start,application_port_end,game_hook,webview,rdp,endpoint_revision,true AS "fresh!"
