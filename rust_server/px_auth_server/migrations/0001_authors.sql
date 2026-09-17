-- Fresh issuer identities. No legacy author or license import.
CREATE TABLE pixels.authors (
    id UUID PRIMARY KEY,
    username_normalized TEXT NOT NULL UNIQUE CHECK (username_normalized <> ''),
    password_hash TEXT NOT NULL CHECK (password_hash <> ''),
    role TEXT NOT NULL CHECK (role IN ('admin', 'visitor')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    authorization_revision BIGINT NOT NULL DEFAULT 1 CHECK (authorization_revision > 0)
);
GRANT SELECT, INSERT, UPDATE, DELETE ON pixels.authors TO pixels_auth_runtime;
