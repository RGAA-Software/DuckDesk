CREATE TABLE pixels.update_releases (
    id UUID PRIMARY KEY CHECK (id <> '00000000-0000-0000-0000-000000000000'),
    registered_by UUID NOT NULL REFERENCES pixels.users(id),
    request_id UUID NOT NULL CHECK (request_id <> '00000000-0000-0000-0000-000000000000'),
    request_hash BYTEA NOT NULL CHECK (octet_length(request_hash)=32),
    product TEXT NOT NULL CHECK (product IN ('cloud_node','client','remote','android','server')),
    distribution TEXT NOT NULL CHECK (distribution IN ('official','customer')),
    channel TEXT NOT NULL CHECK (channel IN ('stable','preview')),
    os TEXT NOT NULL CHECK (os IN ('windows','linux','android')),
    architecture TEXT NOT NULL CHECK (architecture IN ('x86_64','aarch64')),
    build_number BIGINT NOT NULL CHECK (build_number > 0),
    version TEXT NOT NULL CHECK (char_length(version) >= 1 AND char_length(version) <= 64 AND btrim(version) <> ''),
    metadata_base_url TEXT NOT NULL CHECK (char_length(metadata_base_url) >= 9 AND char_length(metadata_base_url) <= 2048
        AND metadata_base_url ~ '^https://[^/@[:space:]]+([/:]|$)' AND metadata_base_url !~ '[?#[:space:]]' AND right(metadata_base_url,1)='/'),
    targets_base_url TEXT NOT NULL CHECK (char_length(targets_base_url) >= 9 AND char_length(targets_base_url) <= 2048
        AND targets_base_url ~ '^https://[^/@[:space:]]+([/:]|$)' AND targets_base_url !~ '[?#[:space:]]' AND right(targets_base_url,1)='/'),
    target_name TEXT NOT NULL CHECK (char_length(target_name) >= 1 AND char_length(target_name) <= 512
        AND target_name !~ '(^/|\\|[[:space:]]|(^|/)\.\.?(/|$)|/$)'),
    sha256 TEXT NOT NULL CHECK (sha256 ~ '^[a-f0-9]{64}$'),
    size_bytes BIGINT NOT NULL CHECK (size_bytes > 0 AND size_bytes <= 1099511627776),
    state TEXT NOT NULL DEFAULT 'pending' CHECK (state IN ('pending','approved','withdrawn')),
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE(registered_by,request_id),
    UNIQUE(product,distribution,channel,os,architecture,build_number),
    CHECK (
        (product='android' AND os='android' AND architecture='aarch64') OR
        (product IN ('cloud_node','client','remote') AND os='windows' AND architecture='x86_64') OR
        (product='server' AND os IN ('windows','linux') AND architecture='x86_64'))
);
GRANT SELECT,INSERT ON pixels.update_releases TO pixels_console_runtime;
GRANT UPDATE(state,revision,updated_at) ON pixels.update_releases TO pixels_console_runtime;

CREATE TABLE pixels.update_release_events (
    id UUID PRIMARY KEY,
    release_id UUID NOT NULL REFERENCES pixels.update_releases(id),
    actor UUID NOT NULL REFERENCES pixels.users(id),
    revision BIGINT NOT NULL CHECK (revision > 0),
    state TEXT NOT NULL CHECK (state IN ('pending','approved','withdrawn')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE(release_id,revision)
);
CREATE INDEX update_release_events_actor ON pixels.update_release_events(actor,id);
GRANT SELECT,INSERT ON pixels.update_release_events TO pixels_console_runtime;

CREATE TABLE pixels.node_update_tasks (
    id UUID PRIMARY KEY CHECK (id <> '00000000-0000-0000-0000-000000000000'),
    node_id UUID NOT NULL REFERENCES pixels.nodes(id),
    release_id UUID NOT NULL REFERENCES pixels.update_releases(id),
    from_build_number BIGINT NOT NULL CHECK (from_build_number > 0),
    to_build_number BIGINT NOT NULL CHECK (to_build_number > from_build_number),
    state TEXT NOT NULL CHECK (state IN ('activating','installed','failed')),
    lease_id UUID NOT NULL CHECK (lease_id <> '00000000-0000-0000-0000-000000000000'),
    lease_until TIMESTAMPTZ NOT NULL,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    error_code TEXT CHECK (
        error_code IS NULL
        OR char_length(error_code) >= 1
            AND char_length(error_code) <= 64
            AND error_code ~ '^[a-z0-9_]+$'
    ),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    completed_at TIMESTAMPTZ,
    CHECK ((state='activating')=(completed_at IS NULL)),
    CHECK ((state='failed')=(error_code IS NOT NULL)),
    CHECK (lease_until > created_at AND lease_until <= created_at + interval '15 minutes')
);
CREATE UNIQUE INDEX node_update_tasks_active ON pixels.node_update_tasks(node_id) WHERE state='activating';
CREATE INDEX node_update_tasks_recent ON pixels.node_update_tasks(node_id,created_at DESC,id DESC);
GRANT SELECT,INSERT ON pixels.node_update_tasks TO pixels_console_runtime;
GRANT UPDATE(state,revision,error_code,updated_at,completed_at) ON pixels.node_update_tasks TO pixels_console_runtime;
