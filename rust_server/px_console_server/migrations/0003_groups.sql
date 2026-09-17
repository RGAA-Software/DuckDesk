CREATE TABLE pixels.user_groups (
    id UUID PRIMARY KEY,
    name TEXT NOT NULL CHECK (char_length(name) BETWEEN 1 AND 128),
    name_normalized TEXT NOT NULL CHECK (name_normalized <> ''),
    remark TEXT NOT NULL DEFAULT '' CHECK (char_length(remark) <= 1024),
    revision BIGINT NOT NULL DEFAULT 1 CHECK (revision > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at TIMESTAMPTZ
);
CREATE UNIQUE INDEX user_groups_active_name ON pixels.user_groups(name_normalized) WHERE deleted_at IS NULL;
CREATE TABLE pixels.group_members (
    group_id UUID NOT NULL REFERENCES pixels.user_groups(id),
    user_id UUID NOT NULL REFERENCES pixels.users(id),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (group_id, user_id)
);
CREATE INDEX group_members_user ON pixels.group_members(user_id, group_id);
GRANT SELECT, INSERT, UPDATE, DELETE ON pixels.user_groups, pixels.group_members TO pixels_console_runtime;
