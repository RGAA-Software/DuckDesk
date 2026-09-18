CREATE TABLE pixels.node_telemetry_latest (
 node_id UUID PRIMARY KEY REFERENCES pixels.nodes(id),
 node_generation BIGINT NOT NULL CHECK(node_generation>0),
 report_sequence BIGINT NOT NULL CHECK(report_sequence>0),
 probe_state TEXT NOT NULL CHECK(probe_state IN ('ready','partial','unavailable')),
 sampled_at TIMESTAMPTZ NOT NULL,
 received_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 logical_processors SMALLINT CHECK(logical_processors>=1),
 cpu_utilization_per_mille SMALLINT CHECK(cpu_utilization_per_mille>=0 AND cpu_utilization_per_mille<=1000),
 memory_total_bytes BIGINT CHECK(memory_total_bytes>=0),
 memory_available_bytes BIGINT CHECK(memory_available_bytes>=0),
 disk_total_bytes BIGINT CHECK(disk_total_bytes>=0),
 disk_free_bytes BIGINT CHECK(disk_free_bytes>=0),
 gpu_inventory_revision BIGINT CHECK(gpu_inventory_revision>0),
 UNIQUE(node_id,gpu_inventory_revision),
 CHECK((memory_total_bytes IS NULL)=(memory_available_bytes IS NULL)),
 CHECK((disk_total_bytes IS NULL)=(disk_free_bytes IS NULL)),
 CHECK(memory_available_bytes IS NULL OR memory_total_bytes IS NULL OR memory_available_bytes<=memory_total_bytes),
 CHECK(disk_free_bytes IS NULL OR disk_total_bytes IS NULL OR disk_free_bytes<=disk_total_bytes),
 CHECK(probe_state<>'ready' OR (
  logical_processors IS NOT NULL AND memory_total_bytes IS NOT NULL AND memory_available_bytes IS NOT NULL AND
  disk_total_bytes IS NOT NULL AND disk_free_bytes IS NOT NULL AND gpu_inventory_revision IS NOT NULL
 )),
 CHECK(probe_state<>'partial' OR (
  logical_processors IS NOT NULL OR cpu_utilization_per_mille IS NOT NULL OR
  memory_total_bytes IS NOT NULL OR disk_total_bytes IS NOT NULL OR gpu_inventory_revision IS NOT NULL
 )),
 CHECK(probe_state<>'unavailable' OR (
  logical_processors IS NULL AND cpu_utilization_per_mille IS NULL AND
  memory_total_bytes IS NULL AND memory_available_bytes IS NULL AND
  disk_total_bytes IS NULL AND disk_free_bytes IS NULL AND gpu_inventory_revision IS NULL
 ))
);

CREATE TABLE pixels.node_gpu_latest (
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 stable_key TEXT NOT NULL CHECK(char_length(stable_key)>=1 AND char_length(stable_key)<=128 AND stable_key!~'[[:space:]]'),
 inventory_revision BIGINT NOT NULL CHECK(inventory_revision>0),
 name TEXT NOT NULL CHECK(char_length(name)>=1 AND char_length(name)<=256),
 dedicated_memory_bytes BIGINT CHECK(dedicated_memory_bytes>=0),
 used_memory_bytes BIGINT CHECK(used_memory_bytes>=0),
 utilization_per_mille SMALLINT CHECK(utilization_per_mille>=0 AND utilization_per_mille<=1000),
 encoder_utilization_per_mille SMALLINT CHECK(encoder_utilization_per_mille>=0 AND encoder_utilization_per_mille<=1000),
 sampled_at TIMESTAMPTZ NOT NULL,
 received_at TIMESTAMPTZ NOT NULL,
 PRIMARY KEY(node_id,stable_key),
 FOREIGN KEY(node_id,inventory_revision)
  REFERENCES pixels.node_telemetry_latest(node_id,gpu_inventory_revision),
 CHECK(used_memory_bytes IS NULL OR dedicated_memory_bytes IS NOT NULL),
 CHECK(used_memory_bytes IS NULL OR dedicated_memory_bytes IS NULL OR used_memory_bytes<=dedicated_memory_bytes)
);

CREATE INDEX node_gpu_latest_inventory ON pixels.node_gpu_latest(node_id,inventory_revision,stable_key);

CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_latest
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();
CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_gpu_latest
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();

GRANT SELECT,INSERT,DELETE ON pixels.node_telemetry_latest,pixels.node_gpu_latest TO pixels_console_runtime;
GRANT UPDATE(node_generation,report_sequence,probe_state,sampled_at,received_at,logical_processors,cpu_utilization_per_mille,
 memory_total_bytes,memory_available_bytes,disk_total_bytes,disk_free_bytes,gpu_inventory_revision)
 ON pixels.node_telemetry_latest TO pixels_console_runtime;
