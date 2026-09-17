SELECT id,revision,state FROM pixels.resource_sessions WHERE node_id=$1 AND closed_at IS NULL ORDER BY id LIMIT 129
