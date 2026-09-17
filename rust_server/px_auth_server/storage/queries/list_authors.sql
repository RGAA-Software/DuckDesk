SELECT id,username_normalized AS username,role,authorization_revision
FROM pixels.authors WHERE ($1::uuid IS NULL OR id>$1) ORDER BY id LIMIT $2
