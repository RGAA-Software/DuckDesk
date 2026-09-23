CREATE TABLE pixels.instance_relays (
    instance_id UUID PRIMARY KEY REFERENCES pixels.instances(id),
    relay_node_id UUID NOT NULL REFERENCES pixels.relay_nodes(id),
    relay_generation BIGINT NOT NULL CHECK (relay_generation > 0),
    bound_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);

CREATE INDEX instance_relays_active_node
ON pixels.instance_relays(relay_node_id, instance_id);

GRANT SELECT, INSERT ON pixels.instance_relays TO pixels_console_runtime;
