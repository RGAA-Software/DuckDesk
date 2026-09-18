SELECT r.id,r.node_id,r.session_id,r.file_name,r.size_bytes,r.modified_at,r.codec,r.reported_present,r.node_generation,
r.control_epoch,r.source_sequence,r.revision,r.created_at,r.observed_at
FROM pixels.recordings r JOIN pixels.resource_sessions s ON s.id=r.session_id
WHERE s.owner_user=$1 AND ($2::uuid IS NULL OR r.id>$2) ORDER BY r.id LIMIT $3
