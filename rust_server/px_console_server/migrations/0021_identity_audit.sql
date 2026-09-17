ALTER TABLE pixels.authorization_audit DROP CONSTRAINT authorization_audit_action_check;
ALTER TABLE pixels.authorization_audit ADD CONSTRAINT authorization_audit_action_check
 CHECK(action IN ('user_created','user_changed','user_deleted','group_membership','group_deleted','password_changed','password_reset','session_revoked'));
ALTER TABLE pixels.authorization_audit ADD COLUMN session_id UUID REFERENCES pixels.login_sessions(id);
ALTER TABLE pixels.authorization_audit ADD CONSTRAINT authorization_audit_session_check CHECK((action='session_revoked')=(session_id IS NOT NULL));
CREATE TABLE pixels.group_events(
 id UUID PRIMARY KEY,
 group_id UUID NOT NULL REFERENCES pixels.user_groups(id),
 actor_id UUID NOT NULL REFERENCES pixels.users(id),
 action TEXT NOT NULL CHECK(action IN ('created','members_changed','deleted')),
 revision BIGINT NOT NULL CHECK(revision>0),
 member_count INTEGER NOT NULL CHECK(member_count>=0 AND member_count<=1000),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(group_id,revision)
);
GRANT SELECT,INSERT ON pixels.group_events TO pixels_console_runtime;

CREATE TABLE pixels.profile_events(
 id UUID PRIMARY KEY,
 user_id UUID NOT NULL REFERENCES pixels.users(id),
 session_id UUID NOT NULL REFERENCES pixels.login_sessions(id),
 revision BIGINT NOT NULL CHECK(revision>0),
 kind TEXT NOT NULL CHECK(kind IN ('username_changed','avatar_changed','avatar_deleted')),
 created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
 UNIQUE(user_id,revision)
);
GRANT SELECT,INSERT ON pixels.profile_events TO pixels_console_runtime;
