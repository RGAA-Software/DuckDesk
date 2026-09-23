SELECT id, name, public_host, public_port, revision, generation, control_epoch, state,
desired_draining, reported_draining, disabled, report_sequence, last_seen, product_version_code,
max_connections, current_connections, max_rooms, current_rooms, uploaded_bytes, forwarded_bytes,
(connection_hash IS NOT NULL AND NOT disabled AND deleted_at IS NULL
    AND control_epoch = (SELECT epoch FROM pixels.control_runtime)
    AND last_seen > clock_timestamp() - interval '30 seconds') AS "fresh!"
FROM pixels.relay_nodes
WHERE deleted_at IS NULL AND ($1::uuid IS NULL OR id > $1)
ORDER BY id
LIMIT $2
