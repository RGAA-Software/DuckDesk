UPDATE pixels.relay_nodes
SET connection_hash = NULL, state = 'offline'
WHERE connection_hash = $1
    AND generation = $2
    AND control_epoch = $3
    AND $3 = (SELECT epoch FROM pixels.control_runtime)
