INSERT INTO pixels.relay_nodes(id, name, public_host, public_port, credential_hash)
VALUES ($1, $2, $3, $4, $5)
RETURNING id, name, public_host, public_port, revision, generation, control_epoch, state,
desired_draining, reported_draining, disabled, report_sequence, last_seen, product_version_code,
max_connections, current_connections, max_rooms, current_rooms, uploaded_bytes, forwarded_bytes,
false AS "fresh!"
