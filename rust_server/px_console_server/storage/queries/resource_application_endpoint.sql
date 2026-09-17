SELECT n.id AS node_id,n.generation,n.control_epoch AS "control_epoch!",n.endpoint_revision,
n.public_host AS "host!",i.port,CASE WHEN i.kind='rdp' THEN 'rdp' ELSE 'native' END AS "transport!"
FROM pixels.instances i JOIN pixels.nodes n ON n.id=i.node_id JOIN pixels.devices d ON d.id=n.device_id
JOIN pixels.applications a ON a.id=i.application_id
WHERE i.id=$1 AND i.application_id=$2 AND (i.owner_user=$3 OR i.owner_guest=$4)
AND i.login_session_id IS NOT DISTINCT FROM $5 AND i.owner_revision=$6 AND i.client_type=$7
AND i.state='running' AND i.desired_state='running' AND i.ended_at IS NULL
AND i.node_generation=n.generation AND i.control_epoch=n.control_epoch AND i.endpoint_revision=n.endpoint_revision
AND n.state='ready' AND NOT n.disabled AND n.deleted_at IS NULL AND NOT d.disabled AND d.deleted_at IS NULL
AND n.control_epoch=(SELECT epoch FROM pixels.control_runtime)
AND n.last_seen>clock_timestamp()-interval '30 seconds' AND n.report_sequence>0 AND n.public_host IS NOT NULL AND NOT a.disabled AND a.deleted_at IS NULL
AND ($8='controller' OR ($8='observer' AND a.allow_observer AND i.kind<>'rdp'))
AND (i.kind<>'rdp' OR ($7='panel' AND $8='controller'))
