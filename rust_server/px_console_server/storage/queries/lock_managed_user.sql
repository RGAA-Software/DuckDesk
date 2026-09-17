SELECT id,username,role,disabled,deleted_at,authorization_revision,revision,created_at FROM pixels.users
WHERE id=$1 FOR UPDATE
