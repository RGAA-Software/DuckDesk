INSERT INTO pixels.authors(id,username_normalized,password_hash,role) VALUES($1,$2,$3,$4)
RETURNING id,username_normalized AS username,role,authorization_revision
