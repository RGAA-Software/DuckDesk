-- A report replaces the complete live Render address snapshot for this node generation.
CREATE TABLE pixels.render_iroh_endpoints (
    node_id UUID NOT NULL REFERENCES pixels.nodes(id),
    node_generation BIGINT NOT NULL CHECK (node_generation > 0),
    control_epoch BIGINT NOT NULL CHECK (control_epoch > 0),
    port INTEGER NOT NULL CHECK (port BETWEEN 1 AND 65535),
    instance_id UUID REFERENCES pixels.instances(id),
    launch_id UUID,
    description TEXT NOT NULL CHECK (octet_length(description) BETWEEN 1 AND 16384),
    reported_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    PRIMARY KEY (node_id, port),
    CHECK ((instance_id IS NULL) = (launch_id IS NULL))
);
GRANT SELECT, INSERT, DELETE ON pixels.render_iroh_endpoints TO pixels_console_runtime;
