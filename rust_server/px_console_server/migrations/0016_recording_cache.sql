CREATE TABLE pixels.cache_roots (
 singleton BOOLEAN PRIMARY KEY DEFAULT true CHECK (singleton),
 id UUID NOT NULL UNIQUE,
 deployment_id UUID NOT NULL,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
CREATE TABLE pixels.cache_runs (
 id UUID PRIMARY KEY,
 root_id UUID NOT NULL REFERENCES pixels.cache_roots(id),
 control_epoch BIGINT NOT NULL REFERENCES pixels.control_runs(epoch),
 byte_limit BIGINT NOT NULL CHECK (byte_limit BETWEEN 1048576 AND 1099511627776),
 max_downloads INTEGER NOT NULL CHECK (max_downloads BETWEEN 1 AND 32),
 ttl_seconds INTEGER NOT NULL CHECK (ttl_seconds BETWEEN 60 AND 604800),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(id,root_id)
);
CREATE TABLE pixels.cache_runtime (
 singleton BOOLEAN PRIMARY KEY DEFAULT true CHECK (singleton),
 run_id UUID NOT NULL REFERENCES pixels.cache_runs(id)
);
CREATE TABLE pixels.recording_cache (
 recording_id UUID PRIMARY KEY REFERENCES pixels.recordings(id),
 active_blob_id UUID,
 pinned BOOLEAN NOT NULL DEFAULT false,
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 last_access_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
CREATE TABLE pixels.cache_blobs (
 id UUID PRIMARY KEY,
 recording_id UUID NOT NULL REFERENCES pixels.recordings(id),
 root_id UUID NOT NULL REFERENCES pixels.cache_roots(id),
 run_id UUID NOT NULL,
 node_generation BIGINT NOT NULL CHECK (node_generation>0),
 origin_user UUID NOT NULL REFERENCES pixels.users(id),
 login_session_id UUID NOT NULL,
 owner_revision BIGINT NOT NULL CHECK (owner_revision>0),
 client_type TEXT NOT NULL CHECK (client_type IN ('panel','android','user_web','admin_web')),
 access_scope TEXT NOT NULL CHECK (access_scope IN ('managed','user')),
 lease_id UUID NOT NULL,
 lease_until TIMESTAMPTZ NOT NULL,
 deadline TIMESTAMPTZ NOT NULL,
 state TEXT NOT NULL CHECK (state IN ('fetching','published','abandoned','deleting','deleted')),
 size_bytes BIGINT NOT NULL CHECK (size_bytes BETWEEN 1 AND 1099511627776),
 source_sha256 BYTEA NOT NULL CHECK (octet_length(source_sha256)=32),
 received_bytes BIGINT NOT NULL DEFAULT 0 CHECK (received_bytes>=0 AND received_bytes<=size_bytes),
 verified_run UUID REFERENCES pixels.cache_runs(id),
 pinned BOOLEAN NOT NULL DEFAULT false,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(id,recording_id),
 FOREIGN KEY(run_id,root_id) REFERENCES pixels.cache_runs(id,root_id),
 FOREIGN KEY(login_session_id,origin_user) REFERENCES pixels.login_sessions(id,user_id),
 CHECK (lease_until<=deadline),
 CHECK ((access_scope='managed' AND client_type='admin_web') OR (access_scope='user' AND client_type IN ('panel','android','user_web'))),
 CHECK (state<>'published' OR received_bytes=size_bytes),
 CHECK (verified_run IS NULL OR state='published')
);
ALTER TABLE pixels.recording_cache ADD FOREIGN KEY(active_blob_id,recording_id) REFERENCES pixels.cache_blobs(id,recording_id);
CREATE UNIQUE INDEX recording_cache_active_blob ON pixels.recording_cache(active_blob_id) WHERE active_blob_id IS NOT NULL;
CREATE INDEX cache_blobs_recording ON pixels.cache_blobs(recording_id,id);
CREATE INDEX cache_blobs_budget ON pixels.cache_blobs(root_id,state) WHERE state<>'deleted';
CREATE INDEX cache_blobs_expiry ON pixels.cache_blobs(lease_until,id) WHERE state='fetching';
CREATE TABLE pixels.cache_events (
 id UUID PRIMARY KEY,
 recording_id UUID NOT NULL REFERENCES pixels.recordings(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 run_id UUID NOT NULL REFERENCES pixels.cache_runs(id),
 blob_id UUID REFERENCES pixels.cache_blobs(id),
 actor_user UUID REFERENCES pixels.users(id),
 kind TEXT NOT NULL CHECK (kind IN ('created','requested','published','abandoned','verified','missing','retained','released','evicted')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(recording_id,revision)
);
GRANT SELECT,INSERT ON pixels.cache_roots,pixels.cache_runs,pixels.cache_runtime,pixels.recording_cache,pixels.cache_blobs,pixels.cache_events TO pixels_console_runtime;
GRANT UPDATE(run_id) ON pixels.cache_runtime TO pixels_console_runtime;
GRANT UPDATE(active_blob_id,pinned,revision,updated_at,last_access_at) ON pixels.recording_cache TO pixels_console_runtime;
GRANT UPDATE(lease_until,state,received_bytes,verified_run,pinned,updated_at) ON pixels.cache_blobs TO pixels_console_runtime;
