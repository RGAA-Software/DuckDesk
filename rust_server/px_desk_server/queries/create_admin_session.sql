INSERT INTO pixels.admin_sessions(id,token_hash,credential_fingerprint,expires_at)
         VALUES($1,$2,$3,clock_timestamp()+interval '8 hours') RETURNING expires_at
