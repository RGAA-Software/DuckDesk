SELECT authorization_revision FROM pixels.users
WHERE id = $1 AND deleted_at IS NULL AND NOT disabled
FOR UPDATE
