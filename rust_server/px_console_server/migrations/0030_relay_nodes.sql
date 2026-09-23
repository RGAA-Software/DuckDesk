CREATE TABLE pixels.relay_nodes (
    id UUID PRIMARY KEY,
    name TEXT NOT NULL UNIQUE CHECK (char_length(name) BETWEEN 1 AND 80),
    public_host TEXT NOT NULL CHECK (char_length(public_host) BETWEEN 1 AND 253),
    public_port INTEGER NOT NULL CHECK (public_port BETWEEN 1 AND 65535),
    credential_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(credential_hash) = 32),
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    generation BIGINT NOT NULL DEFAULT 1 CHECK (generation > 0),
    control_epoch BIGINT REFERENCES pixels.control_runs(epoch),
    connection_hash BYTEA UNIQUE CHECK (octet_length(connection_hash) = 32),
    state TEXT NOT NULL DEFAULT 'offline' CHECK (state IN ('offline', 'syncing', 'ready')),
    desired_draining BOOLEAN NOT NULL DEFAULT true,
    reported_draining BOOLEAN,
    disabled BOOLEAN NOT NULL DEFAULT false,
    report_sequence BIGINT NOT NULL DEFAULT 0 CHECK (report_sequence >= 0),
    last_seen TIMESTAMPTZ,
    product_version_code BIGINT CHECK (product_version_code BETWEEN 1 AND 4294967295),
    max_connections INTEGER CHECK (max_connections BETWEEN 2 AND 100000),
    current_connections INTEGER CHECK (current_connections >= 0 AND current_connections <= max_connections),
    max_rooms INTEGER CHECK (max_rooms BETWEEN 1 AND 50000),
    current_rooms INTEGER CHECK (current_rooms >= 0 AND current_rooms <= max_rooms),
    uploaded_bytes BIGINT CHECK (uploaded_bytes >= 0),
    forwarded_bytes BIGINT CHECK (forwarded_bytes >= 0),
    registered_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    deleted_at TIMESTAMPTZ,
    UNIQUE (public_host, public_port),
    CHECK ((connection_hash IS NULL) = (state = 'offline')),
    CHECK (connection_hash IS NULL OR control_epoch IS NOT NULL),
    CHECK (connection_hash IS NULL OR last_seen IS NOT NULL),
    CHECK (state <> 'ready' OR reported_draining IS NOT NULL),
    CHECK (NOT disabled OR state = 'offline'),
    CHECK (deleted_at IS NULL OR state = 'offline')
);

CREATE TABLE pixels.relay_node_audit (
    id UUID PRIMARY KEY,
    relay_node_id UUID NOT NULL REFERENCES pixels.relay_nodes(id),
    actor_id UUID NOT NULL REFERENCES pixels.users(id),
    revision BIGINT NOT NULL CHECK (revision > 0),
    action TEXT NOT NULL CHECK (action IN ('created', 'configured')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE (relay_node_id, revision)
);

GRANT SELECT, INSERT ON pixels.relay_nodes, pixels.relay_node_audit TO pixels_console_runtime;
GRANT UPDATE(
    revision, generation, control_epoch, connection_hash, state, desired_draining,
    reported_draining, disabled, report_sequence, last_seen, product_version_code,
    max_connections, current_connections, max_rooms, current_rooms, uploaded_bytes,
    forwarded_bytes
) ON pixels.relay_nodes TO pixels_console_runtime;
