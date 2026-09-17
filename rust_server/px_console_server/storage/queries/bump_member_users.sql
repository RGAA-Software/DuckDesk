WITH changed AS (UPDATE pixels.users SET authorization_revision = authorization_revision + 1,
    revision = revision + 1, updated_at = clock_timestamp()
WHERE id = ANY($1) RETURNING id,authorization_revision),
events AS (INSERT INTO pixels.authorization_outbox(id,user_id,authorization_revision,reason)
SELECT gen_random_uuid(),id,authorization_revision,$3 FROM changed RETURNING id)
INSERT INTO pixels.authorization_audit(id,actor_id,subject_id,group_id,action,authorization_revision)
SELECT gen_random_uuid(),$2,id,$4,$3,authorization_revision FROM changed
