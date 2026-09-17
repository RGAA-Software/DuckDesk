SELECT l.id AS license_id,l.customer_id,l.revision,l.revoked_at,i.wire
FROM pixels.licenses l
JOIN LATERAL (SELECT wire FROM pixels.license_issuances WHERE license_id=l.id ORDER BY revision DESC LIMIT 1) i ON true
WHERE ($1::uuid IS NULL OR l.id>$1) ORDER BY l.id LIMIT $2
