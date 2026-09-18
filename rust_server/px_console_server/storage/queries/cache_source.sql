SELECT r.id AS recording_id,r.node_id,n.device_id,r.session_id,r.source_id,r.source_sha256,r.size_bytes,r.node_generation,
COALESCE(r.reported_present AND r.control_epoch=$2 AND r.node_generation=n.generation AND n.control_epoch=$2
 AND $2=(SELECT epoch FROM pixels.control_runtime) AND n.connection_hash IS NOT NULL AND n.report_sequence>0
 AND n.last_seen>clock_timestamp()-interval '30 seconds' AND NOT n.disabled AND n.deleted_at IS NULL
 AND NOT d.disabled AND d.deleted_at IS NULL,false) AS "fetchable!"
FROM pixels.recordings r JOIN pixels.nodes n ON n.id=r.node_id JOIN pixels.devices d ON d.id=n.device_id WHERE r.id=$1
