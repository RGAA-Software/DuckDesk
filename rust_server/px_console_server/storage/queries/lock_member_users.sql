SELECT id, (deleted_at IS NULL AND NOT disabled) AS "active!"
FROM pixels.users WHERE id = ANY($1) ORDER BY id FOR UPDATE
