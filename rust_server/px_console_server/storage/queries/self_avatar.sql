SELECT u.avatar_media_type AS "media_type!",u.avatar_data AS "image_bytes!",u.avatar_sha256 AS "sha256!",u.revision
FROM pixels.users u JOIN pixels.login_sessions s ON s.user_id=u.id
WHERE s.token_hash=$1 AND s.client_type=$2 AND s.revoked_at IS NULL
  AND s.expires_at>clock_timestamp() AND s.absolute_expires_at>clock_timestamp()
  AND u.deleted_at IS NULL AND NOT u.disabled AND u.authorization_revision=s.authorization_revision
  AND u.avatar_data IS NOT NULL
