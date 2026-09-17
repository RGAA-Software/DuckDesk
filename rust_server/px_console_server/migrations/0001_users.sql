-- Greenfield schema. Domain integration follows in DB2; no legacy field adapters.
CREATE TABLE pixels.users (
    id UUID PRIMARY KEY,
    username TEXT NOT NULL CHECK (username <> ''),
    username_normalized TEXT NOT NULL UNIQUE CHECK (username_normalized <> ''),
    password_hash TEXT NOT NULL CHECK (password_hash <> ''),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at TIMESTAMPTZ,
    disabled BOOLEAN NOT NULL DEFAULT FALSE,
    avatar_media_type TEXT,
    avatar_data BYTEA,
    avatar_sha256 BYTEA,
    authorization_revision BIGINT NOT NULL DEFAULT 1 CHECK (authorization_revision > 0),
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    CHECK (
        (avatar_media_type IS NULL AND avatar_data IS NULL AND avatar_sha256 IS NULL)
        OR
        (avatar_media_type IN ('image/png', 'image/jpeg', 'image/webp')
            AND octet_length(avatar_data) BETWEEN 1 AND 2097152
            AND octet_length(avatar_sha256) = 32)
    )
);
CREATE INDEX users_lifecycle ON pixels.users(deleted_at, created_at DESC);
GRANT SELECT, INSERT, UPDATE, DELETE ON pixels.users TO pixels_console_runtime;
