SELECT g.id,g.client_type,g.created_at,g.expires_at,g.revoked_at,g.revision FROM pixels.guest_sessions g
WHERE g.token_hash=$1 AND g.client_type=$2 AND g.revoked_at IS NULL AND g.expires_at>clock_timestamp()
AND NOT EXISTS(SELECT 1 FROM pixels.guest_blocks b WHERE b.guest_id=g.id)
AND NOT EXISTS(SELECT 1 FROM pixels.guest_source_blocks b WHERE b.source_hash=g.source_hash AND b.expires_at>clock_timestamp()) FOR SHARE OF g
