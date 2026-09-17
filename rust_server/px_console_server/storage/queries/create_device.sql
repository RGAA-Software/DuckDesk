INSERT INTO pixels.devices(id,public_code,name,platform,enrollment_hash)
VALUES($1,$2,$3,$4,$5)
RETURNING id,public_code,name,platform,disabled,revision,registered_at
