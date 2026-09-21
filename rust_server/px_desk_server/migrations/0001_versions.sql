-- Fresh Desk schema. No development-data import or legacy API compatibility.
CREATE TABLE pixels.feedback (
    id UUID PRIMARY KEY CHECK (id <> '00000000-0000-0000-0000-000000000000'),
    kind TEXT NOT NULL CHECK (kind IN ('consult','issue')),
    title TEXT NOT NULL CHECK (char_length(title) >= 1 AND char_length(title) <= 128 AND btrim(title) <> ''),
    your_name TEXT NOT NULL CHECK (char_length(your_name) >= 1 AND char_length(your_name) <= 128 AND btrim(your_name) <> ''),
    description TEXT NOT NULL CHECK (char_length(description) >= 1 AND char_length(description) <= 8192 AND btrim(description) <> ''),
    email TEXT NOT NULL CHECK (char_length(email) <= 320),
    wechat TEXT NOT NULL CHECK (char_length(wechat) <= 128),
    qq TEXT NOT NULL CHECK (char_length(qq) <= 64),
    consult_type TEXT,
    version TEXT,
    os TEXT,
    body_sha256 BYTEA NOT NULL CHECK (octet_length(body_sha256)=32),
    processed BOOLEAN NOT NULL DEFAULT false,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    CHECK (
        (kind='consult' AND consult_type IS NOT NULL AND consult_type IN ('personal','enterprise') AND version IS NULL AND os IS NULL)
        OR (kind='issue' AND consult_type IS NULL AND version IS NOT NULL AND os IS NOT NULL
            AND char_length(version) BETWEEN 1 AND 64 AND btrim(version) <> ''
            AND char_length(os) BETWEEN 1 AND 64 AND btrim(os) <> '')
    )
);
CREATE INDEX feedback_recent ON pixels.feedback(kind,created_at DESC,id DESC);
CREATE INDEX feedback_processed_recent ON pixels.feedback(kind,processed,created_at DESC,id DESC);
GRANT SELECT, INSERT ON pixels.feedback TO pixels_desk_runtime;
GRANT UPDATE(processed,revision,updated_at) ON pixels.feedback TO pixels_desk_runtime;

CREATE TABLE pixels.versions (
    id UUID PRIMARY KEY,
    product TEXT NOT NULL CHECK (product IN ('cloud_node','client','remote','android','server')),
    distribution TEXT NOT NULL CHECK (distribution IN ('official','customer')),
    channel TEXT NOT NULL CHECK (channel IN ('stable','preview')),
    build_number BIGINT NOT NULL CHECK (build_number > 0),
    version TEXT NOT NULL CHECK (char_length(version) >= 1 AND char_length(version) <= 64 AND btrim(version) <> ''),
    metadata_base_url TEXT NOT NULL CHECK (char_length(metadata_base_url) >= 9 AND char_length(metadata_base_url) <= 2048
        AND metadata_base_url ~ '^https://[^/@[:space:]]+([/:]|$)' AND metadata_base_url !~ '[?#[:space:]]' AND right(metadata_base_url,1)='/'),
    targets_base_url TEXT NOT NULL CHECK (char_length(targets_base_url) >= 9 AND char_length(targets_base_url) <= 2048
        AND targets_base_url ~ '^https://[^/@[:space:]]+([/:]|$)' AND targets_base_url !~ '[?#[:space:]]' AND right(targets_base_url,1)='/'),
    target_name TEXT NOT NULL CHECK (char_length(target_name) >= 1 AND char_length(target_name) <= 512
        AND target_name !~ '(^/|\\|[[:space:]]|(^|/)\.\.?(/|$)|/$)'),
    sha256 TEXT NOT NULL CHECK (sha256 ~ '^[a-f0-9]{64}$'),
    platform_signer_sha256 TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE(product,distribution,channel,build_number)
);
GRANT SELECT, INSERT ON pixels.versions TO pixels_desk_runtime;

CREATE TABLE pixels.admin_sessions (
    id UUID PRIMARY KEY,
    token_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(token_hash)=32),
    credential_fingerprint BYTEA NOT NULL CHECK (octet_length(credential_fingerprint)=32),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    CHECK (expires_at > created_at AND expires_at <= created_at + interval '8 hours 1 minute')
);
CREATE INDEX admin_sessions_expiration ON pixels.admin_sessions(expires_at);
GRANT SELECT, INSERT ON pixels.admin_sessions TO pixels_desk_runtime;
GRANT UPDATE(revoked_at) ON pixels.admin_sessions TO pixels_desk_runtime;
