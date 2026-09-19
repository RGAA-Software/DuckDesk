CREATE TABLE pixels.license_notification_outbox (
    id UUID PRIMARY KEY,
    license_id UUID NOT NULL REFERENCES pixels.licenses(id),
    revision BIGINT NOT NULL CHECK (revision > 0),
    action TEXT NOT NULL CHECK (action IN ('issued', 'renewed', 'revoked')),
    issuance_id UUID REFERENCES pixels.license_issuances(id),
    available_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    lease_id UUID,
    lease_until TIMESTAMPTZ,
    attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    delivered_at TIMESTAMPTZ,
    last_error TEXT CHECK (last_error IS NULL OR last_error IN ('unavailable', 'rejected')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    UNIQUE (license_id, revision),
    CHECK (
        (action IN ('issued', 'renewed') AND issuance_id IS NOT NULL)
        OR (action = 'revoked' AND issuance_id IS NULL)
    ),
    CHECK (
        (lease_id IS NULL AND lease_until IS NULL)
        OR (lease_id IS NOT NULL AND lease_until IS NOT NULL)
    ),
    CHECK (delivered_at IS NULL OR (lease_id IS NULL AND lease_until IS NULL))
);

CREATE INDEX license_notification_outbox_pending
    ON pixels.license_notification_outbox (available_at, created_at, id)
    WHERE delivered_at IS NULL;

CREATE TRIGGER recovery_security_advance
    AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE
    ON pixels.license_notification_outbox
    FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state();

GRANT SELECT, INSERT, UPDATE ON pixels.license_notification_outbox TO pixels_auth_runtime;
