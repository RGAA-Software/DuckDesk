UPDATE pixels.relay_nodes
SET state = 'ready',
    reported_draining = $5,
    report_sequence = $4,
    last_seen = clock_timestamp(),
    product_version_code = $6,
    max_connections = $7,
    current_connections = $8,
    max_rooms = $9,
    current_rooms = $10,
    uploaded_bytes = $11,
    forwarded_bytes = $12
WHERE connection_hash = $1
    AND generation = $2
    AND control_epoch = $3
    AND $3 = (SELECT epoch FROM pixels.control_runtime)
    AND $4 > report_sequence
    AND NOT disabled
    AND deleted_at IS NULL
RETURNING id, name, public_host, public_port, revision, generation, control_epoch, state,
desired_draining, reported_draining, disabled, report_sequence, last_seen, product_version_code,
max_connections, current_connections, max_rooms, current_rooms, uploaded_bytes, forwarded_bytes,
true AS "fresh!"
