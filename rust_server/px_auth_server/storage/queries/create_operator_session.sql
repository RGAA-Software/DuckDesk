INSERT INTO pixels.author_sessions(id,author_id,token_hash,authorization_revision,expires_at)
             VALUES($1,$2,$3,$4,clock_timestamp()+interval '8 hours') RETURNING expires_at
