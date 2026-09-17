-- Same canonical credential format as px_credentials and Console; no legacy import.
ALTER TABLE pixels.authors
    ADD CONSTRAINT authors_username_bounds CHECK (char_length(username_normalized) >= 2 AND char_length(username_normalized) <= 64),
    ADD CONSTRAINT authors_password_format CHECK (
        octet_length(password_hash) = 97 AND
        password_hash ~ '^\$argon2id\$v=19\$m=19456,t=2,p=1\$[A-Za-z0-9+/]{21}[AQgw]\$[A-Za-z0-9+/]{42}[AEIMQUYcgkosw048]$'
    );
