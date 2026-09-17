INSERT INTO pixels.nodes(id,device_id,product,credential_hash,max_instances)
SELECT $1,d.id,$3,$4,$5 FROM pixels.devices d WHERE d.id=$2 AND NOT d.disabled AND d.deleted_at IS NULL
RETURNING id,device_id,product,revision,generation,control_epoch,state,draining,disabled,max_instances,report_sequence,last_seen,product_version_code,public_host,desktop_port,application_port_start,application_port_end,game_hook,webview,rdp,endpoint_revision, false AS "fresh!"
