CREATE TABLE pixels.recovery_security_state (
    singleton BOOLEAN PRIMARY KEY DEFAULT TRUE CHECK (singleton),
    recovery_generation UUID NOT NULL,
    security_sequence BIGINT NOT NULL CHECK (security_sequence > 0),
    write_barrier_id UUID,
    write_barrier_expires_at TIMESTAMPTZ,
    write_gate_token_sha256 TEXT CHECK (
        write_gate_token_sha256 IS NULL OR write_gate_token_sha256 ~ '^[0-9a-f]{64}$'
    ),
    CHECK (
        (write_barrier_id IS NULL AND write_barrier_expires_at IS NULL AND write_gate_token_sha256 IS NULL)
        OR
        (write_barrier_id IS NOT NULL AND write_barrier_expires_at IS NOT NULL AND write_gate_token_sha256 IS NOT NULL)
    ),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO pixels.recovery_security_state (
    singleton,
    recovery_generation,
    security_sequence
)
VALUES (TRUE, gen_random_uuid(), 1);

CREATE FUNCTION pixels.advance_recovery_security_state()
RETURNS TRIGGER
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, pixels
AS $$
BEGIN
    UPDATE pixels.recovery_security_state
    SET security_sequence = security_sequence + 1,
        write_barrier_id = NULL,
        write_barrier_expires_at = NULL,
        write_gate_token_sha256 = NULL,
        updated_at = CURRENT_TIMESTAMP
    WHERE singleton
      AND (
          write_barrier_id IS NULL
          OR write_barrier_expires_at <= clock_timestamp()
      );
    IF NOT FOUND THEN
        RAISE EXCEPTION 'Pixels coordinated backup write barrier is active'
            USING ERRCODE = '25006';
    END IF;
    RETURN NULL;
END;
$$;

REVOKE ALL ON FUNCTION pixels.advance_recovery_security_state() FROM PUBLIC;

DO $$
DECLARE
    protected_table RECORD;
BEGIN
    FOR protected_table IN
        SELECT table_name
        FROM information_schema.tables
        WHERE table_schema = 'pixels'
          AND table_type = 'BASE TABLE'
          AND table_name NOT IN ('_sqlx_migrations', 'deployment_identity', 'recovery_security_state')
        ORDER BY table_name
    LOOP
        EXECUTE format(
            'CREATE TRIGGER recovery_security_advance AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON pixels.%I FOR EACH STATEMENT EXECUTE FUNCTION pixels.advance_recovery_security_state()',
            protected_table.table_name
        );
    END LOOP;
END;
$$;

GRANT SELECT ON pixels.recovery_security_state TO pixels_console_runtime;
