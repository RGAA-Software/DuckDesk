SELECT binding.relay_node_id,
       binding.relay_generation,
       relay.public_host,
       relay.public_port
FROM pixels.resource_session_relays binding
JOIN pixels.relay_nodes relay ON relay.id = binding.relay_node_id
WHERE binding.session_id = $1
