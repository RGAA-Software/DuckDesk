ALTER TABLE pixels.node_telemetry_history
 DROP CONSTRAINT node_telemetry_history_report_sequence_check;

ALTER TABLE pixels.node_telemetry_history
 ADD CONSTRAINT node_telemetry_history_report_sequence_check CHECK(report_sequence<>0);

CREATE TABLE pixels.node_telemetry_backfill_receipts (
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 sample_id UUID NOT NULL,
 sampled_at TIMESTAMPTZ NOT NULL,
 payload_sha256 BYTEA NOT NULL CHECK(octet_length(payload_sha256)=32),
 received_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 PRIMARY KEY(node_id,sample_id)
);

CREATE INDEX node_telemetry_backfill_receipts_retention
 ON pixels.node_telemetry_backfill_receipts(received_at,node_id,sample_id);

CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_backfill_receipts
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();

GRANT SELECT,INSERT,DELETE ON pixels.node_telemetry_backfill_receipts TO pixels_console_runtime;
