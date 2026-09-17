INSERT INTO pixels.login_sessions(
    id, user_id, token_hash, client_type, authorization_revision, expires_at, absolute_expires_at
)
VALUES (
    $1, $2, $3, $4, $5,
    CURRENT_TIMESTAMP + $6::double precision * INTERVAL '1 second',
    CURRENT_TIMESTAMP + $6::double precision * INTERVAL '1 second'
)
RETURNING id AS session_id, user_id, client_type, authorization_revision, expires_at, absolute_expires_at
