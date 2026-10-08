SELECT application_id, node_id, state, count(*) AS count
FROM pixels.instances
WHERE ended_at IS NULL
GROUP BY application_id, node_id, state
ORDER BY application_id, node_id, state
