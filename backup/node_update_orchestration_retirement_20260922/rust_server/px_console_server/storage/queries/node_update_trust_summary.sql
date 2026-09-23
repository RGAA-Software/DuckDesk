SELECT
    count(*) AS "eligible_node_count!",
    count(*) FILTER (WHERE trust.trusted_root_version >= $2) AS "confirmed_node_count!",
    min(trust.trusted_root_version) FILTER (WHERE trust.trusted_root_version >= $2) AS minimum_confirmed_root_version,
    min(trust.observed_at) FILTER (WHERE trust.trusted_root_version >= $2) AS oldest_confirmation_at
FROM pixels.nodes AS node
LEFT JOIN pixels.node_update_trust AS trust ON trust.node_id=node.id
WHERE node.product=$1 AND node.deleted_at IS NULL
