UPDATE pixels.users SET role=$2,disabled=$3,authorization_revision=authorization_revision+1,revision=revision+1,updated_at=clock_timestamp()
WHERE id=$1 RETURNING id,username,role,disabled,deleted_at,authorization_revision,revision,(avatar_data IS NOT NULL) AS "has_avatar!",created_at
