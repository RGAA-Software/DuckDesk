UPDATE pixels.users
SET avatar_media_type=$2,avatar_data=$3,avatar_sha256=$4,revision=revision+1,updated_at=clock_timestamp()
WHERE id=$1
RETURNING id,username,role,disabled,deleted_at,authorization_revision,revision,(avatar_data IS NOT NULL) AS "has_avatar!",created_at
