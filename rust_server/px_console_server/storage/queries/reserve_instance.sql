WITH candidate AS (
SELECT d.id,d.application_id,d.node_id,d.kind,d.install_root,d.gpu_key,d.revision AS deployment_revision,
a.revision AS application_revision,a.access_revision,a.executable_relative,a.arguments,a.entry_url,a.codec,a.bitrate_kbps,
n.generation,n.control_epoch,n.endpoint_revision,available.port
FROM pixels.application_deployments d JOIN pixels.applications a ON a.id=d.application_id
JOIN pixels.nodes n ON n.id=d.node_id JOIN pixels.devices v ON v.id=n.device_id
CROSS JOIN LATERAL (SELECT count(*) AS used FROM pixels.instances i WHERE i.node_id=n.id AND i.ended_at IS NULL) node_usage
CROSS JOIN LATERAL (SELECT count(*) AS used FROM pixels.instances i WHERE i.deployment_id=d.id AND i.ended_at IS NULL) deployment_usage
CROSS JOIN LATERAL (SELECT p AS port FROM generate_series(n.application_port_start,n.application_port_end) p
 WHERE NOT EXISTS(SELECT 1 FROM pixels.instances i WHERE i.node_id=n.id AND i.port=p AND i.ended_at IS NULL) ORDER BY p LIMIT 1) available
WHERE a.id=$2 AND ($3::uuid IS NULL OR d.id=$3) AND NOT a.disabled AND a.deleted_at IS NULL AND NOT d.disabled
AND NOT n.disabled AND NOT n.draining AND n.deleted_at IS NULL AND NOT v.disabled AND v.deleted_at IS NULL
AND n.state='ready' AND n.connection_hash IS NOT NULL AND n.last_seen>clock_timestamp()-interval '30 seconds'
AND n.control_epoch=$9 AND $9=(SELECT epoch FROM pixels.control_runtime) AND n.public_host IS NOT NULL AND n.report_sequence>0
AND d.observed_state='ready' AND d.observed_generation=n.generation AND d.observed_epoch=n.control_epoch
AND d.observed_endpoint_revision=n.endpoint_revision AND d.application_revision=a.revision
AND ((a.kind='game_hook' AND n.game_hook) OR (a.kind='webview' AND n.webview) OR (a.kind='rdp' AND n.rdp))
AND node_usage.used<n.max_instances AND deployment_usage.used<d.capacity
AND (a.kind<>'rdp' OR NOT EXISTS(SELECT 1 FROM pixels.instances i WHERE i.application_id=a.id AND i.node_id=n.id AND i.ended_at IS NULL))
ORDER BY (n.max_instances-node_usage.used) DESC,d.id LIMIT 1
)
INSERT INTO pixels.instances AS i(id,application_id,deployment_id,node_id,kind,owner_user,owner_guest,client_type,request_id,request_hash,launch_id,
application_revision,application_access_revision,deployment_revision,node_generation,control_epoch,endpoint_revision,port,
install_root,executable_relative,arguments,entry_url,codec,bitrate_kbps,gpu_key,login_session_id,owner_revision)
SELECT $1,c.application_id,c.id,c.node_id,c.kind,$4,$5,$6,$7,$8,$10,c.application_revision,c.access_revision,c.deployment_revision,
c.generation,c.control_epoch,c.endpoint_revision,c.port,c.install_root,c.executable_relative,c.arguments,c.entry_url,c.codec,c.bitrate_kbps,c.gpu_key,$11,$12
FROM candidate c RETURNING i.id,i.application_id,i.node_id,i.owner_user,i.owner_guest,i.client_type,i.request_hash,i.state,i.revision,i.node_generation,i.control_epoch,i.created_at,i.ended_at,i.deployment_id,i.launch_id,i.desired_state,i.application_revision,i.deployment_revision,i.endpoint_revision,i.port
