SELECT
    node.id AS node_id,
    node.device_id,
    node.state AS node_state,
    node.disabled,
    node.last_seen,
    trust.release_id AS "observed_release_id?",
    trust.repository_publication_sha256 AS "repository_publication_sha256?",
    trust.trusted_root_version AS "trusted_root_version?",
    trust.observed_at AS "observed_at?",
    coalesce(trust.trusted_root_version >= $2,false) AS "confirmed!"
FROM pixels.nodes AS node
LEFT JOIN pixels.node_update_trust AS trust ON trust.node_id=node.id
WHERE node.product=$1 AND node.deleted_at IS NULL AND ($3::uuid IS NULL OR node.id>$3)
ORDER BY node.id
LIMIT $4
