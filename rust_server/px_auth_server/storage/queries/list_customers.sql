SELECT id,name,remark FROM pixels.customers WHERE ($1::uuid IS NULL OR id>$1) ORDER BY id LIMIT $2
