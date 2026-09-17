-- First-version single active Console. Authorization mutations and admissions share a transaction gate.
ALTER TABLE pixels.users ADD COLUMN role TEXT NOT NULL DEFAULT 'user' CHECK (role IN ('user','admin','viewer'));
CREATE TABLE pixels.authorization_outbox (
    id UUID PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES pixels.users(id),
    session_id UUID REFERENCES pixels.login_sessions(id),
    authorization_revision BIGINT NOT NULL CHECK (authorization_revision>0),
    reason TEXT NOT NULL CHECK (reason IN ('password_changed','group_membership','group_deleted','user_changed','session_revoked','permissions_changed')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    available_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    lease_id UUID,
    lease_until TIMESTAMPTZ,
    attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts>=0),
    last_error TEXT CHECK (last_error IN ('unavailable','rejected')),
    delivered_at TIMESTAMPTZ,
    CHECK ((lease_id IS NULL)=(lease_until IS NULL)),
    CHECK ((reason='session_revoked')=(session_id IS NOT NULL)),
    UNIQUE NULLS NOT DISTINCT(user_id,session_id,authorization_revision,reason)
);
CREATE INDEX authorization_outbox_pending ON pixels.authorization_outbox(available_at,created_at,id) WHERE delivered_at IS NULL;
CREATE TABLE pixels.authorization_audit (
    id UUID PRIMARY KEY,
    actor_id UUID NOT NULL REFERENCES pixels.users(id),
    subject_id UUID NOT NULL REFERENCES pixels.users(id),
    group_id UUID REFERENCES pixels.user_groups(id),
    action TEXT NOT NULL CHECK (action IN ('user_created','user_changed','user_deleted','group_membership','group_deleted')),
    authorization_revision BIGINT NOT NULL CHECK (authorization_revision>0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    CHECK ((action IN ('group_membership','group_deleted'))=(group_id IS NOT NULL))
);
CREATE INDEX authorization_audit_subject ON pixels.authorization_audit(subject_id,created_at,id);
GRANT SELECT,INSERT ON pixels.authorization_audit TO pixels_console_runtime;
GRANT SELECT,INSERT ON pixels.authorization_outbox TO pixels_console_runtime;
GRANT UPDATE(available_at,lease_id,lease_until,attempts,delivered_at,last_error) ON pixels.authorization_outbox TO pixels_console_runtime;
