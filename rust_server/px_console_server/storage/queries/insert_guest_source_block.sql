INSERT INTO pixels.guest_source_blocks(id,origin_guest_id,source_hash,actor_id,reason,created_at,expires_at)
VALUES($1,$2,$3,$4,$5,statement_timestamp(),statement_timestamp()+make_interval(secs=>$6))
