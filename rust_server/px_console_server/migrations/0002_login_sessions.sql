-- Login identity is explicit; Android is never stored as a Panel identity.
CREATE TABLE pixels.login_sessions (
    id UUID PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES pixels.users(id),
    token_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(token_hash) = 32),
    client_type TEXT NOT NULL CHECK (client_type IN ('panel', 'android', 'user_web', 'admin_web')),
    authorization_revision BIGINT NOT NULL CHECK (authorization_revision > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMPTZ NOT NULL,
    absolute_expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    CHECK (expires_at > created_at AND expires_at <= absolute_expires_at),
    CHECK (absolute_expires_at <= created_at + INTERVAL '365 days')
);
CREATE INDEX login_sessions_user ON pixels.login_sessions(user_id, created_at DESC, id);
CREATE INDEX login_sessions_expiry ON pixels.login_sessions(absolute_expires_at);
GRANT SELECT, INSERT, UPDATE, DELETE ON pixels.login_sessions TO pixels_console_runtime;
