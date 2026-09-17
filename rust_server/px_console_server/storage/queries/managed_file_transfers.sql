SELECT id,session_id,node_id,direction,file_name,total_bytes,transferred_bytes,state,reason,sequence,revision,created_at,updated_at,ended_at FROM pixels.file_transfers
WHERE ($1::uuid IS NULL OR id>$1) AND ($2::uuid IS NULL OR node_id=$2) ORDER BY id LIMIT $3
