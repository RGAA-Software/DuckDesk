SELECT g.id,g.client_type,g.created_at,g.expires_at,g.revoked_at,g.revision,
 (EXISTS(SELECT 1 FROM pixels.guest_blocks b WHERE b.guest_id=g.id)
 OR EXISTS(SELECT 1 FROM pixels.guest_source_blocks b WHERE b.source_hash=g.source_hash AND b.expires_at>clock_timestamp())) AS "blocked!"
FROM pixels.guest_sessions g WHERE ($1::uuid IS NULL OR g.id>$1) ORDER BY g.id LIMIT $2
