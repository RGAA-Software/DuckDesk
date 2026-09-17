UPDATE pixels.users
SET avatar_media_type=NULL,avatar_data=NULL,avatar_sha256=NULL,revision=revision+1,updated_at=clock_timestamp()
WHERE id=$1
RETURNING id,username,role,disabled,deleted_at,authorization_revision,revision,(avatar_data IS NOT NULL) AS "has_avatar!",created_at
