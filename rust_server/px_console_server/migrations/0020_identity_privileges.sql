-- Immutable identities and history are not runtime cleanup targets.
REVOKE UPDATE,DELETE ON pixels.users,pixels.login_sessions,pixels.user_groups,pixels.group_members FROM pixels_console_runtime;
GRANT UPDATE(username,username_normalized,password_hash,updated_at,deleted_at,disabled,avatar_media_type,avatar_data,avatar_sha256,
    authorization_revision,revision,role) ON pixels.users TO pixels_console_runtime;
GRANT UPDATE(revoked_at) ON pixels.login_sessions TO pixels_console_runtime;
GRANT UPDATE(name,name_normalized,remark,revision,updated_at,deleted_at) ON pixels.user_groups TO pixels_console_runtime;
GRANT DELETE ON pixels.group_members TO pixels_console_runtime;
