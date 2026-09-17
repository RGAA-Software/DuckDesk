SELECT id,public_code,name,platform,disabled,revision,registered_at FROM pixels.devices
WHERE deleted_at IS NULL AND ($1::uuid IS NULL OR id>$1) ORDER BY id LIMIT $2
