INSERT INTO pixels.license_requests(author_id,request_id,body_sha256) VALUES($1,$2,$3) ON CONFLICT DO NOTHING
