WITH owned AS (SELECT id,user_id,authorization_revision,revoked_at FROM pixels.login_sessions WHERE id=$1 AND user_id=$2 FOR UPDATE),
changed AS (UPDATE pixels.login_sessions s SET revoked_at=clock_timestamp() FROM owned o
WHERE s.id=o.id AND o.revoked_at IS NULL RETURNING s.id,s.user_id,s.authorization_revision),
events AS (INSERT INTO pixels.authorization_outbox(id,user_id,session_id,authorization_revision,reason)
SELECT gen_random_uuid(),user_id,id,authorization_revision,'session_revoked' FROM changed RETURNING id),
audit AS (INSERT INTO pixels.authorization_audit(id,actor_id,subject_id,session_id,action,authorization_revision)
SELECT gen_random_uuid(),user_id,user_id,id,'session_revoked',authorization_revision FROM changed RETURNING id)
SELECT count(*) AS "found!" FROM owned
