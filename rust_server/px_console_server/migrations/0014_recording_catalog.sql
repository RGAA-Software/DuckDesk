CREATE TABLE pixels.recordings (
 id UUID PRIMARY KEY,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 source_id UUID NOT NULL,
 source_sha256 BYTEA NOT NULL CHECK (octet_length(source_sha256)=32),
 session_id UUID,
 file_name TEXT NOT NULL CHECK (octet_length(file_name)>=1 AND octet_length(file_name)<=255),
 size_bytes BIGINT NOT NULL CHECK (size_bytes>0),
 modified_at TIMESTAMPTZ NOT NULL,
 codec TEXT NOT NULL CHECK (codec IN ('h264','h265','av1','unknown')),
 metadata_hash BYTEA NOT NULL CHECK (octet_length(metadata_hash)=32),
 reported_present BOOLEAN NOT NULL,
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 source_sequence BIGINT NOT NULL CHECK (source_sequence>0),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 observed_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(node_id,source_id),
 FOREIGN KEY(session_id,node_id) REFERENCES pixels.resource_sessions(id,node_id)
);
CREATE INDEX recordings_node ON pixels.recordings(node_id,id);
CREATE INDEX recordings_session ON pixels.recordings(session_id,id) WHERE session_id IS NOT NULL;
CREATE TABLE pixels.recording_events (
 id UUID PRIMARY KEY,
 recording_id UUID NOT NULL REFERENCES pixels.recordings(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 source_sequence BIGINT NOT NULL CHECK (source_sequence>0),
 reported_present BOOLEAN NOT NULL,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(recording_id,revision)
);
GRANT SELECT,INSERT ON pixels.recordings,pixels.recording_events TO pixels_console_runtime;
GRANT UPDATE(reported_present,node_generation,control_epoch,source_sequence,revision,observed_at) ON pixels.recordings TO pixels_console_runtime;
