UPDATE pixels.resource_sessions
SET descriptor_expires_at=clock_timestamp()+interval '30 seconds'
WHERE id=$1 AND revision=$2 AND descriptor_hash=$3
AND state IN ('pending','connected') AND descriptor_expires_at>clock_timestamp()
RETURNING descriptor_expires_at AS "expires_at!",
GREATEST(0,LEAST(30000,FLOOR(EXTRACT(EPOCH FROM descriptor_expires_at-clock_timestamp())*1000)))::bigint AS "valid_for_ms!"
