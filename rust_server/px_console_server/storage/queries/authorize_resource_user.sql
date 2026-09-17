SELECT u.id FROM pixels.login_sessions s JOIN pixels.users u ON u.id=s.user_id
WHERE s.token_hash=$1 AND s.client_type=$2 AND s.client_type IN ('panel','android','user_web')
  AND s.revoked_at IS NULL AND s.expires_at>clock_timestamp() AND s.absolute_expires_at>clock_timestamp()
  AND u.role IN ('user','admin') AND NOT u.disabled AND u.deleted_at IS NULL
  AND u.authorization_revision=s.authorization_revision
FOR SHARE OF u,s
