INSERT INTO pixels.cache_blobs(id,recording_id,root_id,run_id,node_generation,origin_user,login_session_id,owner_revision,client_type,
access_scope,lease_id,lease_until,deadline,state,size_bytes,source_sha256)
SELECT $1,r.id,$3,$4,r.node_generation,$5,$6,$7,$8,$9,$10,clock_timestamp()+interval '30 seconds',
clock_timestamp()+interval '15 minutes','fetching',r.size_bytes,r.source_sha256 FROM pixels.recordings r WHERE r.id=$2
RETURNING id,recording_id,root_id,run_id,node_generation,lease_id,lease_until,state,size_bytes,source_sha256,received_bytes,verified_run,
(lease_until<=clock_timestamp() OR deadline<=clock_timestamp()) AS "expired!",
GREATEST(0,LEAST(30000,FLOOR(EXTRACT(EPOCH FROM (LEAST(lease_until,deadline)-clock_timestamp()))*1000)))::bigint AS "remaining_ms!"
