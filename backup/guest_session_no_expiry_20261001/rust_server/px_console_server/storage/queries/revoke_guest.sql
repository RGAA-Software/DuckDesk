UPDATE pixels.guest_sessions SET revoked_at=coalesce(revoked_at,clock_timestamp()),revision=revision+1 WHERE id=$1 RETURNING id,client_type,created_at,expires_at,revoked_at,revision
