INSERT INTO pixels.users(id,username,username_normalized,password_hash,role) VALUES($1,$2,$3,$4,$5)
RETURNING id,username,role,disabled,deleted_at,authorization_revision,revision,created_at
