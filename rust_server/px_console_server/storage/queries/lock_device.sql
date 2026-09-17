SELECT id,public_code,name,platform,disabled,revision,registered_at
FROM pixels.devices WHERE id=$1 AND deleted_at IS NULL FOR UPDATE
