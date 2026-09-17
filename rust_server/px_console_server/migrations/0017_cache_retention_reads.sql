CREATE TABLE pixels.cache_read_leases (
 id UUID PRIMARY KEY,
 blob_id UUID NOT NULL REFERENCES pixels.cache_blobs(id),
 run_id UUID NOT NULL REFERENCES pixels.cache_runs(id),
 origin_user UUID NOT NULL REFERENCES pixels.users(id),
 login_session_id UUID NOT NULL,
 owner_revision BIGINT NOT NULL CHECK(owner_revision>0),
 client_type TEXT NOT NULL CHECK(client_type IN ('panel','android','user_web','admin_web')),
 access_scope TEXT NOT NULL CHECK(access_scope IN ('managed','device')),
 expires_at TIMESTAMPTZ NOT NULL,
 closed_at TIMESTAMPTZ,
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 FOREIGN KEY(login_session_id,origin_user) REFERENCES pixels.login_sessions(id,user_id),
 CHECK((access_scope='managed' AND client_type='admin_web') OR (access_scope='device' AND client_type IN ('panel','android','user_web')))
);
CREATE INDEX cache_read_leases_blob ON pixels.cache_read_leases(blob_id,expires_at) WHERE closed_at IS NULL;
CREATE INDEX cache_read_leases_login ON pixels.cache_read_leases(login_session_id,expires_at) WHERE closed_at IS NULL;
GRANT SELECT,INSERT ON pixels.cache_read_leases TO pixels_console_runtime;
GRANT UPDATE(expires_at,closed_at) ON pixels.cache_read_leases TO pixels_console_runtime;
ALTER TABLE pixels.cache_events DROP CONSTRAINT cache_events_kind_check;
ALTER TABLE pixels.cache_events ADD CHECK (kind IN ('created','requested','published','abandoned','verified','missing','retained','released','evicted','collected'));
