UPDATE pixels.resource_sessions SET descriptor_hash=$2,descriptor_expires_at=clock_timestamp()+interval '30 seconds',revision=revision+1
WHERE id=$1 AND state IN ('pending','connected')
RETURNING revision,descriptor_expires_at AS "expires_at!"
