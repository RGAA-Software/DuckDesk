INSERT INTO pixels.cache_read_leases(id,blob_id,run_id,origin_user,login_session_id,owner_revision,client_type,access_scope,expires_at)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,clock_timestamp()+interval '30 seconds')
RETURNING id,blob_id,run_id,expires_at,
GREATEST(0,LEAST(30000,FLOOR(EXTRACT(EPOCH FROM (expires_at-clock_timestamp()))*1000)))::bigint AS "remaining_ms!"
