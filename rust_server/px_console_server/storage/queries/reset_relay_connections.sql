UPDATE pixels.relay_nodes
SET connection_hash = NULL, state = 'offline'
WHERE connection_hash IS NOT NULL
