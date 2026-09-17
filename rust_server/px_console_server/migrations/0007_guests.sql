CREATE TABLE pixels.guest_sessions (
 id UUID PRIMARY KEY,
 token_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(token_hash)=32),
 source_hash BYTEA NOT NULL CHECK (octet_length(source_hash)=32),
 client_type TEXT NOT NULL CHECK (client_type IN ('panel','android','user_web')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 expires_at TIMESTAMPTZ NOT NULL,
 revoked_at TIMESTAMPTZ,
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 CHECK (expires_at>created_at AND expires_at<=created_at+interval '1 day'),
 UNIQUE(id,source_hash)
);
CREATE INDEX guest_sessions_expiry ON pixels.guest_sessions(expires_at);
CREATE INDEX guest_sessions_source ON pixels.guest_sessions(source_hash,id);
CREATE TABLE pixels.guest_source_blocks (
 id UUID PRIMARY KEY,
 origin_guest_id UUID NOT NULL,
 source_hash BYTEA NOT NULL,
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 reason TEXT NOT NULL CHECK (reason IN ('operator','abuse')),
 created_at TIMESTAMPTZ NOT NULL,
 expires_at TIMESTAMPTZ NOT NULL,
 FOREIGN KEY(origin_guest_id,source_hash) REFERENCES pixels.guest_sessions(id,source_hash),
 CHECK (expires_at>created_at AND expires_at<=created_at+interval '1 day')
);
CREATE INDEX guest_source_blocks_expiry ON pixels.guest_source_blocks(source_hash,expires_at);
CREATE TABLE pixels.guest_blocks (
 guest_id UUID PRIMARY KEY REFERENCES pixels.guest_sessions(id),
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 reason TEXT NOT NULL CHECK (reason IN ('operator','abuse')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
CREATE TABLE pixels.guest_events (
 id UUID PRIMARY KEY,
 guest_id UUID NOT NULL REFERENCES pixels.guest_sessions(id),
 actor_id UUID REFERENCES pixels.users(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 reason TEXT NOT NULL CHECK (reason IN ('logout','blocked','source_blocked')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 available_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 lease_id UUID,
 lease_until TIMESTAMPTZ,
 attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts>=0),
 last_error TEXT CHECK (last_error IN ('unavailable','rejected')),
 delivered_at TIMESTAMPTZ,
 CHECK ((reason='logout')=(actor_id IS NULL)),
 CHECK ((lease_id IS NULL)=(lease_until IS NULL)),
 UNIQUE(guest_id,revision)
);
CREATE INDEX guest_events_pending ON pixels.guest_events(available_at,created_at,id) WHERE delivered_at IS NULL;
GRANT SELECT,INSERT ON pixels.guest_sessions,pixels.guest_blocks,pixels.guest_source_blocks,pixels.guest_events TO pixels_console_runtime;
GRANT UPDATE(revoked_at,revision) ON pixels.guest_sessions TO pixels_console_runtime;
GRANT UPDATE(available_at,lease_id,lease_until,attempts,last_error,delivered_at) ON pixels.guest_events TO pixels_console_runtime;
