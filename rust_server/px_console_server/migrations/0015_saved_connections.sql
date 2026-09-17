CREATE TABLE pixels.saved_connections (
 id UUID PRIMARY KEY,
 owner_id UUID NOT NULL REFERENCES pixels.users(id),
 client_type TEXT NOT NULL CHECK (client_type IN ('panel','android','user_web')),
 request_id UUID NOT NULL,
 request_hash BYTEA NOT NULL CHECK (octet_length(request_hash)=32),
 name TEXT NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
 device_id UUID REFERENCES pixels.devices(id),
 application_id UUID REFERENCES pixels.applications(id),
 video_bitrate_bps BIGINT NOT NULL CHECK (video_bitrate_bps BETWEEN 256000 AND 200000000),
 video_fps INTEGER NOT NULL CHECK (video_fps BETWEEN 1 AND 240),
 audio_enabled BOOLEAN NOT NULL,
 clipboard_enabled BOOLEAN NOT NULL,
 view_only BOOLEAN NOT NULL,
 maximize BOOLEAN NOT NULL,
 split_windows BOOLEAN NOT NULL,
 prefer_peer_to_peer BOOLEAN NOT NULL,
 audio_capture TEXT NOT NULL CHECK (audio_capture IN ('system_mix','target_application')),
 background_rgb INTEGER NOT NULL CHECK (background_rgb BETWEEN 0 AND 16777215),
 revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 deleted_at TIMESTAMPTZ,
 CHECK ((device_id IS NOT NULL)::integer+(application_id IS NOT NULL)::integer=1),
 UNIQUE(owner_id,client_type,request_id)
);
CREATE INDEX saved_connections_owner ON pixels.saved_connections(owner_id,client_type,id) WHERE deleted_at IS NULL;
CREATE INDEX saved_connections_device ON pixels.saved_connections(device_id) WHERE device_id IS NOT NULL;
CREATE INDEX saved_connections_application ON pixels.saved_connections(application_id) WHERE application_id IS NOT NULL;
CREATE TABLE pixels.saved_connection_events (
 id UUID PRIMARY KEY,
 connection_id UUID NOT NULL REFERENCES pixels.saved_connections(id),
 revision BIGINT NOT NULL CHECK (revision>0),
 kind TEXT NOT NULL CHECK (kind IN ('created','updated','deleted')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(connection_id,revision)
);
GRANT SELECT,INSERT ON pixels.saved_connections,pixels.saved_connection_events TO pixels_console_runtime;
GRANT UPDATE(name,video_bitrate_bps,video_fps,audio_enabled,clipboard_enabled,view_only,maximize,split_windows,
 prefer_peer_to_peer,audio_capture,background_rgb,revision,updated_at,deleted_at) ON pixels.saved_connections TO pixels_console_runtime;
