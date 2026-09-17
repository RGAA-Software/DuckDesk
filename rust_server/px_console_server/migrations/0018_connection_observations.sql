CREATE TABLE pixels.connection_observations (
 id UUID PRIMARY KEY,
 source_id UUID NOT NULL,
 session_id UUID NOT NULL,
 node_id UUID NOT NULL,
 node_generation BIGINT NOT NULL CHECK(node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 kind TEXT NOT NULL CHECK(kind IN ('control','media','audio','file','rdp')),
 request_hash BYTEA NOT NULL CHECK(octet_length(request_hash)=32),
 state TEXT NOT NULL DEFAULT 'active' CHECK(state IN ('active','closed','failed','unknown')),
 reason TEXT CHECK(reason IN ('peer_closed','user_stopped','transport_lost','policy_revoked','io_error','control_lost','frontend_closed')),
 sent_bytes BIGINT NOT NULL DEFAULT 0 CHECK(sent_bytes>=0),
 received_bytes BIGINT NOT NULL DEFAULT 0 CHECK(received_bytes>=0),
 elapsed_ms BIGINT NOT NULL DEFAULT 0 CHECK(elapsed_ms>=0),
 sequence BIGINT NOT NULL DEFAULT 0 CHECK(sequence>=0),
 report_hash BYTEA CHECK(octet_length(report_hash)=32),
 revision BIGINT NOT NULL DEFAULT 1 CHECK(revision>0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 ended_at TIMESTAMPTZ,
 UNIQUE(node_id,source_id),
 FOREIGN KEY(session_id,node_id) REFERENCES pixels.resource_sessions(id,node_id),
 CHECK((report_hash IS NULL)=(sequence=0)),
 CHECK((state='active')=(reason IS NULL)),
 CHECK((ended_at IS NOT NULL)=(state IN ('closed','failed')))
);
CREATE INDEX connection_observations_session ON pixels.connection_observations(session_id,id);
CREATE INDEX connection_observations_node_active ON pixels.connection_observations(node_id,id) WHERE state='active';
CREATE TABLE pixels.connection_observation_events (
 id UUID PRIMARY KEY,
 connection_id UUID NOT NULL REFERENCES pixels.connection_observations(id),
 revision BIGINT NOT NULL CHECK(revision>0),
 sequence BIGINT NOT NULL CHECK(sequence>=0),
 state TEXT NOT NULL CHECK(state IN ('active','closed','failed','unknown')),
 sent_bytes BIGINT NOT NULL CHECK(sent_bytes>=0),
 received_bytes BIGINT NOT NULL CHECK(received_bytes>=0),
 elapsed_ms BIGINT NOT NULL CHECK(elapsed_ms>=0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(connection_id,revision)
);
GRANT SELECT,INSERT ON pixels.connection_observations,pixels.connection_observation_events TO pixels_console_runtime;
GRANT UPDATE(state,reason,sent_bytes,received_bytes,elapsed_ms,sequence,report_hash,revision,updated_at,ended_at)
 ON pixels.connection_observations TO pixels_console_runtime;
