WITH pending AS (SELECT id FROM pixels.authorization_outbox
WHERE delivered_at IS NULL AND available_at<=clock_timestamp() AND (lease_until IS NULL OR lease_until<=clock_timestamp())
ORDER BY created_at,id FOR UPDATE SKIP LOCKED LIMIT $1)
UPDATE pixels.authorization_outbox e SET lease_id=$2,lease_until=clock_timestamp()+interval '30 seconds',attempts=attempts+1
FROM pending p WHERE e.id=p.id
RETURNING e.id,e.user_id,e.session_id,e.authorization_revision,e.reason,e.lease_id AS "lease_id!",e.attempts
