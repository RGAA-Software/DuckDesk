WITH active_bindings AS (
    SELECT relay_node_id, count(*)::bigint AS session_count
    FROM (
        SELECT binding.relay_node_id, binding.instance_id AS owner_id
        FROM pixels.instance_relays binding
        JOIN pixels.instances instance ON instance.id = binding.instance_id
        WHERE instance.ended_at IS NULL
        UNION ALL
        SELECT binding.relay_node_id, binding.session_id AS owner_id
        FROM pixels.resource_session_relays binding
        JOIN pixels.resource_sessions session ON session.id = binding.session_id
        WHERE session.closed_at IS NULL AND session.target_kind = 'desktop'
    ) binding
    GROUP BY relay_node_id
)
SELECT relay.id AS relay_node_id,
       relay.generation AS relay_generation,
       relay.public_host,
       relay.public_port
FROM pixels.relay_nodes relay
LEFT JOIN active_bindings usage ON usage.relay_node_id = relay.id
WHERE relay.state = 'ready'
  AND relay.connection_hash IS NOT NULL
  AND relay.control_epoch = $1
  AND relay.last_seen > clock_timestamp() - interval '30 seconds'
  AND NOT relay.disabled
  AND NOT relay.desired_draining
  AND relay.reported_draining = false
  AND relay.deleted_at IS NULL
  AND relay.max_connections IS NOT NULL
  AND relay.current_connections IS NOT NULL
  AND relay.max_rooms IS NOT NULL
  AND relay.current_rooms IS NOT NULL
  AND greatest(relay.current_connections::bigint, coalesce(usage.session_count, 0) * 2) + 2 <= relay.max_connections
  AND greatest(relay.current_rooms::bigint, coalesce(usage.session_count, 0)) + 1 <= relay.max_rooms
ORDER BY greatest(
             greatest(relay.current_connections::bigint, coalesce(usage.session_count, 0) * 2)::numeric / relay.max_connections,
             greatest(relay.current_rooms::bigint, coalesce(usage.session_count, 0))::numeric / relay.max_rooms
         ),
         relay.id
LIMIT 1
