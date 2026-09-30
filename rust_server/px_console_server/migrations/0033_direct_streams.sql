CREATE TABLE pixels.direct_streams (
    id UUID PRIMARY KEY,
    node_id UUID NOT NULL REFERENCES pixels.nodes(id),
    device_id UUID NOT NULL REFERENCES pixels.devices(id),
    node_generation BIGINT NOT NULL CHECK (node_generation > 0),
    control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    FOREIGN KEY (node_id, device_id) REFERENCES pixels.nodes(id, device_id)
);
CREATE INDEX direct_streams_active ON pixels.direct_streams(expires_at, node_id);
GRANT SELECT, INSERT, DELETE ON pixels.direct_streams TO pixels_console_runtime;
GRANT UPDATE(expires_at) ON pixels.direct_streams TO pixels_console_runtime;
