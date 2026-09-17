SELECT c.id,c.source_id,c.session_id,c.node_id,c.node_generation,c.control_epoch,c.kind,c.request_hash,c.state,c.reason,c.sent_bytes,c.received_bytes,c.elapsed_ms,c.sequence,c.report_hash,c.revision,c.created_at,c.updated_at,c.ended_at FROM pixels.connection_observations c JOIN pixels.resource_sessions s ON s.id=c.session_id
WHERE (s.owner_user=$1 OR s.owner_guest=$2) AND s.client_type=$3 AND ($4::uuid IS NULL OR c.id>$4)
AND ($5::uuid IS NULL OR c.session_id=$5) ORDER BY c.id LIMIT $6
