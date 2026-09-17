SELECT id,username,role,disabled,deleted_at,authorization_revision,revision,(avatar_data IS NOT NULL) AS "has_avatar!",created_at FROM pixels.users
WHERE id=$1 FOR UPDATE
