CREATE TABLE pixels.devices (
    id UUID PRIMARY KEY,
    public_code TEXT NOT NULL UNIQUE CHECK (public_code ~ '^[0-9]{12}$'),
    name TEXT NOT NULL CHECK (char_length(name)>=1 AND char_length(name)<=128 AND name=btrim(name)),
    platform TEXT NOT NULL CHECK (platform IN ('windows','linux','macos','android')),
    enrollment_hash BYTEA NOT NULL UNIQUE CHECK (octet_length(enrollment_hash)=32),
    disabled BOOLEAN NOT NULL DEFAULT false,
    deleted_at TIMESTAMPTZ,
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision>0),
    registered_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);
CREATE TABLE pixels.user_devices (
    user_id UUID NOT NULL REFERENCES pixels.users(id),
    device_id UUID NOT NULL REFERENCES pixels.devices(id),
    PRIMARY KEY(user_id,device_id)
);
CREATE INDEX user_devices_device ON pixels.user_devices(device_id,user_id);
CREATE TABLE pixels.group_device_grants (
    group_id UUID NOT NULL REFERENCES pixels.user_groups(id),
    device_id UUID NOT NULL REFERENCES pixels.devices(id),
    PRIMARY KEY(group_id,device_id)
);
CREATE INDEX group_device_grants_device ON pixels.group_device_grants(device_id,group_id);
CREATE TABLE pixels.device_audit (
    id UUID PRIMARY KEY,
    actor_id UUID NOT NULL REFERENCES pixels.users(id),
    device_id UUID NOT NULL REFERENCES pixels.devices(id),
    revision BIGINT NOT NULL CHECK (revision>0),
    action TEXT NOT NULL CHECK (action IN ('created','updated','access_changed','key_rotated','deleted')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE(device_id,revision)
);
CREATE INDEX device_audit_actor ON pixels.device_audit(actor_id,created_at,id);
GRANT SELECT,INSERT ON pixels.devices,pixels.device_audit TO pixels_console_runtime;
GRANT UPDATE(name,enrollment_hash,disabled,deleted_at,revision,updated_at) ON pixels.devices TO pixels_console_runtime;
GRANT SELECT,INSERT,DELETE ON pixels.user_devices,pixels.group_device_grants TO pixels_console_runtime;
