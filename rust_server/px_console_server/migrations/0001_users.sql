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
    avatar_path TEXT NOT NULL DEFAULT '',
    authorization_revision BIGINT NOT NULL DEFAULT 1 CHECK (authorization_revision > 0),
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0)
);
CREATE INDEX users_lifecycle ON pixels.users(deleted_at, created_at DESC);
GRANT SELECT, INSERT, UPDATE, DELETE ON pixels.users TO pixels_console_runtime;
