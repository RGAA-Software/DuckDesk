-- Application policy is snapshotted into each launch; existing runtimes retain their launch policy.
ALTER TABLE pixels.applications ADD COLUMN disconnect_grace_seconds integer NOT NULL DEFAULT 10
    CHECK (disconnect_grace_seconds BETWEEN 1 AND 3600);
ALTER TABLE pixels.instances ADD COLUMN disconnect_grace_seconds integer NOT NULL DEFAULT 10
    CHECK (disconnect_grace_seconds BETWEEN 1 AND 3600);
GRANT UPDATE (disconnect_grace_seconds) ON pixels.applications TO pixels_console_runtime;
