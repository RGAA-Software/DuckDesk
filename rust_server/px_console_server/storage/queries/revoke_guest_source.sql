WITH selected AS (SELECT id FROM pixels.guest_sessions
 WHERE source_hash=$1 AND (id=$2 OR (revoked_at IS NULL AND expires_at>clock_timestamp())) ORDER BY id FOR UPDATE),
changed AS (UPDATE pixels.guest_sessions g SET revoked_at=coalesce(g.revoked_at,clock_timestamp()),revision=g.revision+1
 FROM selected s WHERE g.id=s.id RETURNING g.id,g.revision)
INSERT INTO pixels.guest_events(id,guest_id,actor_id,revision,reason)
SELECT gen_random_uuid(),id,$3,revision,'source_blocked' FROM changed
