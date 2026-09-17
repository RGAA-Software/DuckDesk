SELECT id,client_type,created_at,expires_at,revoked_at,revision FROM pixels.guest_sessions WHERE id=$1
