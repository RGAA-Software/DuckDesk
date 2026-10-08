INSERT INTO pixels.guest_sessions(id,token_hash,client_type,created_at,source_hash)
SELECT $1,$2,$3,statement_timestamp(),$4
WHERE NOT EXISTS(SELECT 1 FROM pixels.guest_source_blocks WHERE source_hash=$4 AND expires_at>clock_timestamp())
RETURNING id,client_type,created_at,revoked_at,revision
