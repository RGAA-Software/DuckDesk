SELECT u.id,u.role,s.id AS session_id FROM pixels.login_sessions s JOIN pixels.users u ON u.id=s.user_id
WHERE s.token_hash=$1 AND s.client_type='admin_web' AND s.revoked_at IS NULL
  AND s.expires_at>clock_timestamp() AND s.absolute_expires_at>clock_timestamp()
  AND u.deleted_at IS NULL AND NOT u.disabled AND u.authorization_revision=s.authorization_revision
FOR SHARE OF u,s
