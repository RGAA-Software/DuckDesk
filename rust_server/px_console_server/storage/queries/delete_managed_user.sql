UPDATE pixels.users SET deleted_at=clock_timestamp(),authorization_revision=authorization_revision+1,revision=revision+1,updated_at=clock_timestamp()
WHERE id=$1 RETURNING authorization_revision
