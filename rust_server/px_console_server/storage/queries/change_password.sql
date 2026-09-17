WITH changed AS (UPDATE pixels.users
SET password_hash = $3, authorization_revision = authorization_revision + 1,
    revision = revision + 1, updated_at = clock_timestamp()
WHERE id = $1 AND authorization_revision = $2 AND deleted_at IS NULL AND NOT disabled
RETURNING id,authorization_revision),
events AS (INSERT INTO pixels.authorization_outbox(id,user_id,authorization_revision,reason)
SELECT gen_random_uuid(),id,authorization_revision,'password_changed' FROM changed RETURNING id)
SELECT authorization_revision AS "authorization_revision!" FROM changed
