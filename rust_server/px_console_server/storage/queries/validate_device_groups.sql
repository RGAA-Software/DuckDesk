SELECT id FROM pixels.user_groups WHERE id=ANY($1) AND deleted_at IS NULL ORDER BY id FOR UPDATE
