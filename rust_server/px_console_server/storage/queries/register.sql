INSERT INTO pixels.users(id, username, username_normalized, password_hash)
VALUES ($1, $2, $3, $4)
RETURNING id, username, authorization_revision, revision, created_at
