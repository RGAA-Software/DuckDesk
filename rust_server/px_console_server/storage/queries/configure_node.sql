UPDATE pixels.nodes SET draining=$2,disabled=$3,max_instances=$4,revision=revision+1,
generation=generation+CASE WHEN disabled IS DISTINCT FROM $3 THEN 1 ELSE 0 END,
connection_hash=CASE WHEN $3 THEN NULL ELSE connection_hash END,state=CASE WHEN $3 THEN 'offline' ELSE state END
,reconciliation_id=CASE WHEN $3 THEN NULL ELSE reconciliation_id END,reconciliation_deadline=CASE WHEN $3 THEN NULL ELSE reconciliation_deadline END WHERE id=$1
RETURNING id,device_id,product,revision,generation,control_epoch,state,draining,disabled,max_instances,report_sequence,last_seen,product_version_code,public_host,desktop_port,application_port_start,application_port_end,game_hook,webview,rdp,endpoint_revision,
(connection_hash IS NOT NULL AND NOT disabled AND control_epoch=(SELECT epoch FROM pixels.control_runtime) AND last_seen>clock_timestamp()-interval '30 seconds') AS "fresh!"
