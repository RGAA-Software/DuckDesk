CREATE TABLE pixels.resource_session_relays (
    session_id UUID PRIMARY KEY REFERENCES pixels.resource_sessions(id),
    relay_node_id UUID NOT NULL REFERENCES pixels.relay_nodes(id),
    relay_generation BIGINT NOT NULL CHECK (relay_generation > 0),
    bound_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);

CREATE INDEX resource_session_relays_active_node
ON pixels.resource_session_relays(relay_node_id, session_id);

GRANT SELECT, INSERT ON pixels.resource_session_relays TO pixels_console_runtime;
