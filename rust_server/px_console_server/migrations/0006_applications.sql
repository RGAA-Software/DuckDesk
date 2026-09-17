CREATE TABLE pixels.applications (
 id UUID PRIMARY KEY,
 name TEXT NOT NULL CHECK (char_length(name)>=1 AND char_length(name)<=128 AND name=btrim(name)),
 kind TEXT NOT NULL CHECK (kind IN ('game_hook','webview','rdp')),
 access_mode TEXT NOT NULL CHECK (access_mode IN ('public','acl')),
 entry_url TEXT CHECK (octet_length(entry_url)<=8192),
 executable_relative TEXT CHECK (octet_length(executable_relative)<=2048),
 arguments TEXT CHECK (octet_length(arguments)<=8192),
 bitrate_kbps INTEGER CHECK (bitrate_kbps>=128 AND bitrate_kbps<=200000),
 codec TEXT CHECK (codec IN ('h264','h265')),
 allow_observer BOOLEAN NOT NULL,
 allow_takeover BOOLEAN NOT NULL,
 disabled BOOLEAN NOT NULL,
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 access_revision BIGINT NOT NULL DEFAULT 1 CHECK (access_revision>0 AND access_revision<=revision),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 deleted_at TIMESTAMPTZ,
 CHECK ((kind='game_hook' AND executable_relative IS NOT NULL AND executable_relative<>'' AND arguments IS NOT NULL AND entry_url IS NULL AND bitrate_kbps IS NOT NULL AND codec IS NOT NULL)
 OR (kind='webview' AND executable_relative IS NULL AND arguments IS NULL AND entry_url IS NOT NULL AND entry_url<>'' AND bitrate_kbps IS NOT NULL AND codec IS NOT NULL)
 OR (kind='rdp' AND executable_relative IS NULL AND arguments IS NULL AND entry_url IS NULL AND bitrate_kbps IS NULL AND codec IS NULL AND NOT allow_observer AND NOT allow_takeover))
);
CREATE TABLE pixels.group_app_grants (
 group_id UUID NOT NULL REFERENCES pixels.user_groups(id),
 application_id UUID NOT NULL REFERENCES pixels.applications(id),
 PRIMARY KEY(group_id,application_id)
);
CREATE INDEX group_app_grants_application ON pixels.group_app_grants(application_id,group_id);
-- The immutable event fields also provide an append-only administration audit.
CREATE TABLE pixels.application_events (
 id UUID PRIMARY KEY,
 application_id UUID NOT NULL REFERENCES pixels.applications(id),
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 access_revision BIGINT NOT NULL CHECK (access_revision>0 AND access_revision<=revision),
 kind TEXT NOT NULL CHECK (kind IN ('created','updated','grants_changed','deleted')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 available_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 lease_id UUID,
 lease_until TIMESTAMPTZ,
 attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts>=0),
 last_error TEXT CHECK (last_error IN ('unavailable','rejected')),
 delivered_at TIMESTAMPTZ,
 CHECK ((lease_id IS NULL)=(lease_until IS NULL)),
 UNIQUE(application_id,revision)
);
CREATE INDEX application_events_pending ON pixels.application_events(available_at,created_at,id) WHERE delivered_at IS NULL;
GRANT SELECT,INSERT ON pixels.applications,pixels.application_events TO pixels_console_runtime;
GRANT UPDATE(name,access_mode,entry_url,executable_relative,arguments,bitrate_kbps,codec,allow_observer,allow_takeover,disabled,revision,access_revision,updated_at,deleted_at)
 ON pixels.applications TO pixels_console_runtime;
GRANT UPDATE(available_at,lease_id,lease_until,attempts,last_error,delivered_at) ON pixels.application_events TO pixels_console_runtime;
GRANT SELECT,INSERT,DELETE ON pixels.group_app_grants TO pixels_console_runtime;
