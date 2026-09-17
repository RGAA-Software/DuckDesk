SELECT id,client_type,created_at,expires_at,revoked_at,revision FROM pixels.guest_sessions WHERE token_hash=$1 AND client_type=$2 FOR UPDATE
