SELECT revision,revoked_at FROM pixels.licenses WHERE id=$1 FOR UPDATE
