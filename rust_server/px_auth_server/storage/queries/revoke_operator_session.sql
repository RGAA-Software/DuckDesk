UPDATE pixels.author_sessions SET revoked_at=clock_timestamp() WHERE token_hash=$1 AND revoked_at IS NULL
