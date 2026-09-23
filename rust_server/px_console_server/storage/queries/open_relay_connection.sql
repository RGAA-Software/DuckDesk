UPDATE pixels.relay_nodes
SET connection_hash = $2,
    generation = generation + 1,
    control_epoch = $3,
    state = 'syncing',
    reported_draining = NULL,
    report_sequence = 0,
    last_seen = clock_timestamp()
WHERE credential_hash = $1
    AND NOT disabled
    AND deleted_at IS NULL
    AND $3 = (SELECT epoch FROM pixels.control_runtime)
RETURNING id, name, public_host, public_port, revision, generation, control_epoch, state,
desired_draining, reported_draining, disabled, report_sequence, last_seen, product_version_code,
max_connections, current_connections, max_rooms, current_rooms, uploaded_bytes, forwarded_bytes,
true AS "fresh!"
