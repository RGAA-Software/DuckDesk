-- Fresh credential policy. No plaintext, old hash format or unbounded identity fields.
-- The terminal base64 characters also enforce zero unused bits (canonical 16/32-byte values).
ALTER TABLE pixels.users
    ADD CONSTRAINT users_username_bounds CHECK (char_length(username) >= 2 AND char_length(username) <= 64),
    ADD CONSTRAINT users_normalized_bounds CHECK (char_length(username_normalized) >= 2 AND char_length(username_normalized) <= 64),
    ADD CONSTRAINT users_password_format CHECK (
        octet_length(password_hash) = 97 AND
        password_hash ~ '^\$argon2id\$v=19\$m=19456,t=2,p=1\$[A-Za-z0-9+/]{21}[AQgw]\$[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]$'
    );
