UPDATE pixels.cache_blobs SET verified_run=$2,updated_at=clock_timestamp() WHERE id=$1 RETURNING id,recording_id,root_id,run_id,node_generation,lease_id,lease_until,state,size_bytes,source_sha256,received_bytes,verified_run,
(lease_until<=clock_timestamp() OR deadline<=clock_timestamp()) AS "expired!",
GREATEST(0,LEAST(30000,FLOOR(EXTRACT(EPOCH FROM (LEAST(lease_until,deadline)-clock_timestamp()))*1000)))::bigint AS "remaining_ms!"
