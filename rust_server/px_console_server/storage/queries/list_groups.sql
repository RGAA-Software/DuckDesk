SELECT id,name,remark,revision FROM pixels.user_groups
WHERE deleted_at IS NULL AND ($1::uuid IS NULL OR id>$1) ORDER BY id LIMIT $2
