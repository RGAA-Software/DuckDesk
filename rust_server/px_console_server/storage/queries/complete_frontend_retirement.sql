UPDATE pixels.resource_session_retirements SET completed_at=COALESCE(completed_at,clock_timestamp())
WHERE session_id=$1 AND challenge_id=$2 AND session_revision=$3 AND node_generation=$4 AND control_epoch=$5
AND (completed_at IS NOT NULL OR deadline>clock_timestamp()) RETURNING session_id
