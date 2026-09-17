UPDATE pixels.admin_sessions SET revoked_at=clock_timestamp() WHERE id=$1 AND revoked_at IS NULL
