SELECT id FROM pixels.admin_sessions WHERE token_hash=$1 AND credential_fingerprint=$2
         AND revoked_at IS NULL AND expires_at>clock_timestamp()
