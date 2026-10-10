ALTER TABLE pixels.relay_nodes
    ADD COLUMN iroh_qad_port INTEGER CHECK (iroh_qad_port BETWEEN 1 AND 65535),
    ADD CONSTRAINT relay_iroh_has_no_rooms CHECK (
        iroh_qad_port IS NULL OR (max_rooms IS NULL AND current_rooms IS NULL)
    );

GRANT UPDATE(iroh_qad_port) ON pixels.relay_nodes TO pixels_console_runtime;
