CREATE TABLE pixels.file_transfers (
 id UUID PRIMARY KEY,
 node_id UUID NOT NULL REFERENCES pixels.nodes(id),
 session_id UUID NOT NULL,
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 request_id UUID NOT NULL,
 request_hash BYTEA NOT NULL CHECK (octet_length(request_hash)=32),
 direction TEXT NOT NULL CHECK (direction IN ('to_node','from_node')),
 file_name TEXT NOT NULL CHECK (octet_length(file_name)>=1 AND octet_length(file_name)<=255),
 total_bytes BIGINT NOT NULL CHECK (total_bytes>=0),
 expected_sha256 BYTEA CHECK (octet_length(expected_sha256)=32),
 received_sha256 BYTEA CHECK (octet_length(received_sha256)=32),
 transferred_bytes BIGINT NOT NULL DEFAULT 0 CHECK (transferred_bytes>=0 AND transferred_bytes<=total_bytes),
 state TEXT NOT NULL DEFAULT 'active' CHECK (state IN ('active','completed','failed','cancelled','unknown')),
 reason TEXT CHECK (reason IN ('cancelled','transport_lost','hash_mismatch','policy_revoked','io_error','source_changed','control_lost','frontend_closed')),
 sequence BIGINT NOT NULL DEFAULT 0 CHECK (sequence>=0),
 report_hash BYTEA CHECK (octet_length(report_hash)=32),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 ended_at TIMESTAMPTZ,
 UNIQUE(node_id,request_id),
 FOREIGN KEY(session_id,node_id) REFERENCES pixels.resource_sessions(id,node_id),
 CHECK ((report_hash IS NULL)=(sequence=0)),
 CHECK ((ended_at IS NOT NULL)=(state IN ('completed','failed','cancelled'))),
 CHECK ((state IN ('active','completed'))=(reason IS NULL)),
 CHECK (state<>'completed' OR (transferred_bytes=total_bytes AND received_sha256 IS NOT NULL AND expected_sha256=received_sha256)),
 CHECK ((received_sha256 IS NOT NULL)=(state='completed'))
);
CREATE INDEX file_transfers_session ON pixels.file_transfers(session_id,id);
CREATE INDEX file_transfers_node_active ON pixels.file_transfers(node_id,id) WHERE state='active';
CREATE TABLE pixels.file_transfer_events (
 id UUID PRIMARY KEY,
 transfer_id UUID NOT NULL REFERENCES pixels.file_transfers(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 sequence BIGINT NOT NULL CHECK (sequence>=0),
 transferred_bytes BIGINT NOT NULL CHECK (transferred_bytes>=0),
 kind TEXT NOT NULL CHECK (kind IN ('created','progress','completed','failed','cancelled','unknown')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(transfer_id,revision)
);
GRANT SELECT,INSERT ON pixels.file_transfers,pixels.file_transfer_events TO pixels_console_runtime;
GRANT UPDATE(expected_sha256,received_sha256,transferred_bytes,state,reason,sequence,report_hash,revision,updated_at,ended_at) ON pixels.file_transfers TO pixels_console_runtime;
