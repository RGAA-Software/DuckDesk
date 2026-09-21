UPDATE pixels.node_update_tasks
SET state=$4,error_code=$5,revision=revision+1,updated_at=clock_timestamp(),completed_at=clock_timestamp()
WHERE id=$1 AND lease_id=$2 AND revision=$3 AND state='activating' AND lease_until>clock_timestamp()
RETURNING state,revision,error_code
