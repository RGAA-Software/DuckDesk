SELECT id, username, authorization_revision, revision, created_at, password_hash, role
FROM pixels.users
WHERE username_normalized = $1 AND deleted_at IS NULL AND NOT disabled
