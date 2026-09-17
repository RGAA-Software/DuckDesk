SELECT s.id AS session_id, s.user_id, s.client_type, s.authorization_revision, s.expires_at, s.absolute_expires_at
FROM pixels.login_sessions s
JOIN pixels.users u ON u.id = s.user_id
WHERE s.token_hash = $1 AND s.client_type = $2 AND s.revoked_at IS NULL
    AND s.expires_at > clock_timestamp() AND s.absolute_expires_at > clock_timestamp()
    AND u.deleted_at IS NULL AND NOT u.disabled
    AND s.authorization_revision = u.authorization_revision
