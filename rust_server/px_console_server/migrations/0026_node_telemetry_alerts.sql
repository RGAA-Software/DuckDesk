CREATE TABLE pixels.node_telemetry_alert_policies (
 node_id UUID PRIMARY KEY REFERENCES pixels.nodes(id) ON DELETE CASCADE,
 revision BIGINT NOT NULL DEFAULT 1 CHECK(revision>0),
 cpu_warning_per_mille SMALLINT NOT NULL DEFAULT 850,
 cpu_critical_per_mille SMALLINT NOT NULL DEFAULT 950,
 memory_warning_per_mille SMALLINT NOT NULL DEFAULT 850,
 memory_critical_per_mille SMALLINT NOT NULL DEFAULT 950,
 disk_warning_per_mille SMALLINT NOT NULL DEFAULT 850,
 disk_critical_per_mille SMALLINT NOT NULL DEFAULT 950,
 gpu_warning_per_mille SMALLINT NOT NULL DEFAULT 900,
 gpu_critical_per_mille SMALLINT NOT NULL DEFAULT 980,
 trigger_samples SMALLINT NOT NULL DEFAULT 3 CHECK(trigger_samples>=1 AND trigger_samples<=60),
 recovery_samples SMALLINT NOT NULL DEFAULT 3 CHECK(recovery_samples>=1 AND recovery_samples<=60),
 recovery_hysteresis_per_mille SMALLINT NOT NULL DEFAULT 50 CHECK(recovery_hysteresis_per_mille>=1 AND recovery_hysteresis_per_mille<=250),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 CHECK(cpu_warning_per_mille>=1 AND cpu_warning_per_mille<cpu_critical_per_mille AND cpu_critical_per_mille<=1000),
 CHECK(memory_warning_per_mille>=1 AND memory_warning_per_mille<memory_critical_per_mille AND memory_critical_per_mille<=1000),
 CHECK(disk_warning_per_mille>=1 AND disk_warning_per_mille<disk_critical_per_mille AND disk_critical_per_mille<=1000),
 CHECK(gpu_warning_per_mille>=1 AND gpu_warning_per_mille<gpu_critical_per_mille AND gpu_critical_per_mille<=1000),
 CHECK(recovery_hysteresis_per_mille<LEAST(cpu_warning_per_mille,memory_warning_per_mille,disk_warning_per_mille,gpu_warning_per_mille))
);

INSERT INTO pixels.node_telemetry_alert_policies(node_id)
SELECT id FROM pixels.nodes;

CREATE TABLE pixels.node_telemetry_alert_events (
 id UUID PRIMARY KEY,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id) ON DELETE CASCADE,
 metric TEXT NOT NULL CHECK(metric IN ('cpu','memory','disk','gpu')),
 resource_key TEXT NOT NULL CHECK(char_length(resource_key)>=1 AND char_length(resource_key)<=128),
 resource_name TEXT NOT NULL CHECK(char_length(resource_name)>=1 AND char_length(resource_name)<=256),
 severity TEXT NOT NULL CHECK(severity IN ('warning','critical')),
 state TEXT NOT NULL CHECK(state IN ('active','acknowledged','recovered')),
 threshold_per_mille SMALLINT NOT NULL CHECK(threshold_per_mille>=1 AND threshold_per_mille<=1000),
 first_value_per_mille SMALLINT NOT NULL CHECK(first_value_per_mille>=0 AND first_value_per_mille<=1000),
 latest_value_per_mille SMALLINT NOT NULL CHECK(latest_value_per_mille>=0 AND latest_value_per_mille<=1000),
 peak_value_per_mille SMALLINT NOT NULL CHECK(peak_value_per_mille>=0 AND peak_value_per_mille<=1000),
 occurrence_count BIGINT NOT NULL DEFAULT 1 CHECK(occurrence_count>0),
 first_sampled_at TIMESTAMPTZ NOT NULL,
 last_sampled_at TIMESTAMPTZ NOT NULL,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 acknowledged_by UUID REFERENCES pixels.users(id),
 acknowledged_at TIMESTAMPTZ,
 recovered_at TIMESTAMPTZ,
 revision BIGINT NOT NULL DEFAULT 1 CHECK(revision>0),
 UNIQUE(id,node_id,metric,resource_key),
 CHECK((state='active' AND acknowledged_by IS NULL AND acknowledged_at IS NULL AND recovered_at IS NULL) OR
       (state='acknowledged' AND acknowledged_by IS NOT NULL AND acknowledged_at IS NOT NULL AND recovered_at IS NULL) OR
       (state='recovered' AND recovered_at IS NOT NULL)),
 CHECK(last_sampled_at>=first_sampled_at),
 CHECK(peak_value_per_mille>=first_value_per_mille AND peak_value_per_mille>=latest_value_per_mille)
);

CREATE UNIQUE INDEX node_telemetry_alert_one_open_condition
 ON pixels.node_telemetry_alert_events(node_id,metric,resource_key) WHERE state<>'recovered';
CREATE INDEX node_telemetry_alert_events_page
 ON pixels.node_telemetry_alert_events(updated_at DESC,id DESC);
CREATE INDEX node_telemetry_alert_events_node_page
 ON pixels.node_telemetry_alert_events(node_id,updated_at DESC,id DESC);

CREATE TABLE pixels.node_telemetry_alert_conditions (
 node_id UUID NOT NULL REFERENCES pixels.nodes(id) ON DELETE CASCADE,
 metric TEXT NOT NULL CHECK(metric IN ('cpu','memory','disk','gpu')),
 resource_key TEXT NOT NULL CHECK(char_length(resource_key)>=1 AND char_length(resource_key)<=128),
 resource_name TEXT NOT NULL CHECK(char_length(resource_name)>=1 AND char_length(resource_name)<=256),
 breach_samples SMALLINT NOT NULL DEFAULT 0 CHECK(breach_samples>=0 AND breach_samples<=60),
 recovery_samples SMALLINT NOT NULL DEFAULT 0 CHECK(recovery_samples>=0 AND recovery_samples<=60),
 open_event_id UUID,
 last_value_per_mille SMALLINT NOT NULL CHECK(last_value_per_mille>=0 AND last_value_per_mille<=1000),
 last_sampled_at TIMESTAMPTZ NOT NULL,
 node_generation BIGINT NOT NULL CHECK(node_generation>0),
 report_sequence BIGINT NOT NULL CHECK(report_sequence>0),
 PRIMARY KEY(node_id,metric,resource_key),
 FOREIGN KEY(open_event_id,node_id,metric,resource_key)
  REFERENCES pixels.node_telemetry_alert_events(id,node_id,metric,resource_key)
);

CREATE TABLE pixels.node_telemetry_alert_policy_audit (
 id UUID PRIMARY KEY,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id) ON DELETE CASCADE,
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 policy_revision BIGINT NOT NULL CHECK(policy_revision>1),
 occurred_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(node_id,policy_revision)
);

CREATE TABLE pixels.node_telemetry_alert_audit (
 id UUID PRIMARY KEY,
 event_id UUID NOT NULL REFERENCES pixels.node_telemetry_alert_events(id) ON DELETE CASCADE,
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 action TEXT NOT NULL CHECK(action='acknowledged'),
 event_revision BIGINT NOT NULL CHECK(event_revision>0),
 occurred_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);

CREATE INDEX node_telemetry_alert_audit_event
 ON pixels.node_telemetry_alert_audit(event_id,occurred_at,id);

CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_alert_policies
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();
CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_alert_events
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();
CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_alert_conditions
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();
CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_alert_policy_audit
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();
CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.node_telemetry_alert_audit
 FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();

GRANT SELECT,INSERT,UPDATE ON pixels.node_telemetry_alert_policies TO pixels_console_runtime;
GRANT SELECT,INSERT,UPDATE,DELETE ON pixels.node_telemetry_alert_events,pixels.node_telemetry_alert_conditions TO pixels_console_runtime;
GRANT SELECT,INSERT ON pixels.node_telemetry_alert_policy_audit TO pixels_console_runtime;
GRANT SELECT,INSERT ON pixels.node_telemetry_alert_audit TO pixels_console_runtime;
