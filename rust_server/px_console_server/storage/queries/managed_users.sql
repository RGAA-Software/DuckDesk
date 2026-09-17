SELECT id,username,role,disabled,deleted_at,authorization_revision,revision,created_at FROM pixels.users
WHERE ($1::uuid IS NULL OR id>$1) ORDER BY id LIMIT $2
