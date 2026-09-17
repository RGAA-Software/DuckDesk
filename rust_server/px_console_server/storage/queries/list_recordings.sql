SELECT id,node_id,session_id,file_name,size_bytes,modified_at,codec,reported_present,node_generation,control_epoch,source_sequence,revision,created_at,observed_at FROM pixels.recordings
WHERE ($1::uuid IS NULL OR node_id=$1) AND ($2::uuid IS NULL OR id>$2) ORDER BY id LIMIT $3
