SELECT EXISTS(
SELECT 1 FROM pixels.instances i
JOIN pixels.applications a ON a.id=i.application_id
JOIN pixels.application_deployments d ON d.id=i.deployment_id
JOIN pixels.nodes n ON n.id=i.node_id JOIN pixels.devices v ON v.id=n.device_id
WHERE i.id=$1 AND i.ended_at IS NULL AND i.desired_state='running' AND i.state IN ('reserved','starting')
AND NOT a.disabled AND a.deleted_at IS NULL AND a.revision=i.application_revision AND a.access_revision=i.application_access_revision
AND NOT d.disabled AND d.revision=i.deployment_revision AND d.application_revision=a.revision
AND d.observed_state='ready' AND d.observed_generation=n.generation AND d.observed_epoch=n.control_epoch
AND d.observed_endpoint_revision=n.endpoint_revision
AND NOT n.disabled AND NOT n.draining AND n.deleted_at IS NULL AND n.state='ready'
AND NOT EXISTS(SELECT 1 FROM pixels.node_update_tasks u WHERE u.node_id=n.id AND u.state='activating')
AND n.connection_hash IS NOT NULL AND n.last_seen>clock_timestamp()-interval '30 seconds'
AND n.generation=i.node_generation AND n.control_epoch=i.control_epoch AND n.control_epoch=(SELECT epoch FROM pixels.control_runtime)
AND n.endpoint_revision=i.endpoint_revision AND i.port>=n.application_port_start AND i.port<=n.application_port_end
AND NOT v.disabled AND v.deleted_at IS NULL
AND ((a.kind='game_hook' AND n.game_hook) OR (a.kind='webview' AND n.webview) OR (a.kind='rdp' AND n.rdp))
) AS "eligible!"
