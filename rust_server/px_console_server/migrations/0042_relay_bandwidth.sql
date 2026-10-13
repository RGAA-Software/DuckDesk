ALTER TABLE pixels.relay_nodes
    ADD COLUMN bandwidth_capacity_mbps INTEGER CHECK (bandwidth_capacity_mbps BETWEEN 1 AND 100000),
    ADD COLUMN forwarded_rate_bps BIGINT CHECK (forwarded_rate_bps >= 0);

GRANT UPDATE(bandwidth_capacity_mbps, forwarded_rate_bps)
ON pixels.relay_nodes TO pixels_console_runtime;
