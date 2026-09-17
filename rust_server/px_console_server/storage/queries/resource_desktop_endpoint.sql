SELECT n.id AS node_id,n.generation,n.control_epoch AS "control_epoch!",n.endpoint_revision,
n.public_host AS "host!",n.desktop_port AS "port!",'native'::text AS "transport!"
FROM pixels.nodes n JOIN pixels.devices d ON d.id=n.device_id
WHERE d.id=$1 AND n.state='ready' AND NOT n.disabled AND n.deleted_at IS NULL AND NOT d.disabled AND d.deleted_at IS NULL
AND n.control_epoch=(SELECT epoch FROM pixels.control_runtime)
AND n.last_seen>clock_timestamp()-interval '30 seconds' AND n.report_sequence>0
AND n.public_host IS NOT NULL AND n.desktop_port IS NOT NULL
