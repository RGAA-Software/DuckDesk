CREATE TABLE pixels.author_sessions (
    id UUID PRIMARY KEY,
    author_id UUID NOT NULL REFERENCES pixels.authors(id),
    token_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(token_hash)=32),
    authorization_revision BIGINT NOT NULL CHECK (authorization_revision>0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ,
    CHECK (expires_at > created_at AND expires_at <= created_at + interval '8 hours 1 minute')
);
CREATE INDEX author_sessions_expiry ON pixels.author_sessions(expires_at);
CREATE TABLE pixels.customers (
    id UUID PRIMARY KEY,
    name TEXT NOT NULL CHECK (char_length(name)>=1 AND char_length(name)<=128),
    name_normalized TEXT NOT NULL UNIQUE,
    remark TEXT NOT NULL CHECK (char_length(remark)<=1024),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
CREATE TABLE pixels.licenses (
    id UUID PRIMARY KEY,
    customer_id UUID NOT NULL REFERENCES pixels.customers(id),
    target_deployment UUID NOT NULL,
    revision BIGINT NOT NULL CHECK (revision>0),
    expires_at TIMESTAMPTZ NOT NULL,
    max_streams BIGINT NOT NULL CHECK (max_streams>0 AND max_streams<=4294967295),
    services TEXT[] NOT NULL CHECK (cardinality(services)>=1 AND cardinality(services)<=3 AND services <@ ARRAY['cloud_applications','desktop','rdp']::text[]),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    revoked_at TIMESTAMPTZ,
    CHECK (expires_at>created_at)
);
CREATE UNIQUE INDEX licenses_active_deployment ON pixels.licenses(target_deployment) WHERE revoked_at IS NULL;
CREATE INDEX licenses_customer ON pixels.licenses(customer_id,created_at DESC,id);
CREATE TABLE pixels.license_issuances (
    id UUID PRIMARY KEY,
    license_id UUID NOT NULL REFERENCES pixels.licenses(id),
    revision BIGINT NOT NULL CHECK (revision>0),
    key_id TEXT NOT NULL CHECK (key_id ~ '^[a-f0-9]{64}$'),
    payload BYTEA NOT NULL CHECK (octet_length(payload)<=4096),
    wire TEXT NOT NULL CHECK (char_length(wire)<=8192 AND wire LIKE 'PXLIC2.%'),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE(license_id,revision)
);
CREATE TABLE pixels.license_requests (
    author_id UUID NOT NULL REFERENCES pixels.authors(id),
    request_id UUID NOT NULL,
    body_sha256 BYTEA NOT NULL CHECK (octet_length(body_sha256)=32),
    issuance_id UUID REFERENCES pixels.license_issuances(id),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    PRIMARY KEY(author_id,request_id)
);
CREATE TABLE pixels.license_audit (
    id UUID PRIMARY KEY,
    author_id UUID NOT NULL REFERENCES pixels.authors(id),
    license_id UUID NOT NULL REFERENCES pixels.licenses(id),
    action TEXT NOT NULL CHECK (action IN ('issued','renewed','revoked')),
    revision BIGINT NOT NULL CHECK (revision>0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
GRANT SELECT, INSERT ON pixels.customers,pixels.license_issuances,pixels.license_audit TO pixels_auth_runtime;
GRANT SELECT, INSERT, UPDATE ON pixels.author_sessions,pixels.licenses,pixels.license_requests TO pixels_auth_runtime;
