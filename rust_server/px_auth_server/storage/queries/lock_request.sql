SELECT body_sha256,issuance_id FROM pixels.license_requests WHERE author_id=$1 AND request_id=$2 FOR UPDATE
