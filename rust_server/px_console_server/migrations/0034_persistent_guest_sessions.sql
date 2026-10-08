-- Guest identities persist until explicitly revoked or blocked. Resource descriptors,
-- user login sessions and timed source blocks keep their independent expiry rules.
ALTER TABLE pixels.guest_sessions DROP COLUMN expires_at;
