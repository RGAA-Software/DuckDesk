SELECT t.id,t.session_id,t.node_id,t.direction,t.file_name,t.total_bytes,t.transferred_bytes,t.state,t.reason,t.sequence,t.revision,t.created_at,t.updated_at,t.ended_at FROM pixels.file_transfers t JOIN pixels.resource_sessions s ON s.id=t.session_id
WHERE (s.owner_user=$1 OR s.owner_guest=$2) AND s.client_type=$3 AND ($4::uuid IS NULL OR t.id>$4) ORDER BY t.id LIMIT $5
