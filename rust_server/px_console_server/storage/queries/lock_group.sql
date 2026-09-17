SELECT id, name, remark, revision FROM pixels.user_groups WHERE id = $1 AND deleted_at IS NULL FOR UPDATE
