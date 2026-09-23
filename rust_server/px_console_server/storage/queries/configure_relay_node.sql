UPDATE pixels.relay_nodes
SET desired_draining = $2,
    disabled = $3,
    revision = revision + 1,
    generation = generation + CASE WHEN disabled IS DISTINCT FROM $3 THEN 1 ELSE 0 END,
    connection_hash = CASE WHEN $3 THEN NULL ELSE connection_hash END,
    state = CASE WHEN $3 THEN 'offline' ELSE state END
WHERE id = $1
RETURNING id, name, public_host, public_port, revision, generation, control_epoch, state,
desired_draining, reported_draining, disabled, report_sequence, last_seen, product_version_code,
max_connections, current_connections, max_rooms, current_rooms, uploaded_bytes, forwarded_bytes,
(connection_hash IS NOT NULL AND NOT disabled
    AND control_epoch = (SELECT epoch FROM pixels.control_runtime)
    AND last_seen > clock_timestamp() - interval '30 seconds') AS "fresh!"
