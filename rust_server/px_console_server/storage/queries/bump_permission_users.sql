WITH changed AS (
 UPDATE pixels.users SET authorization_revision=authorization_revision+1,revision=revision+1,updated_at=clock_timestamp()
 WHERE id=ANY($1) RETURNING id,authorization_revision
)
INSERT INTO pixels.authorization_outbox(id,user_id,authorization_revision,reason)
SELECT gen_random_uuid(),id,authorization_revision,'permissions_changed' FROM changed
