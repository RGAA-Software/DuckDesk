SELECT id FROM pixels.authors WHERE id=ANY($1) ORDER BY id FOR UPDATE
