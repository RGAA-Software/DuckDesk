SELECT product_version_code AS "product_version_code!",draining,
EXISTS(SELECT 1 FROM pixels.instances WHERE node_id=$1 AND ended_at IS NULL)
 OR EXISTS(SELECT 1 FROM pixels.resource_sessions WHERE node_id=$1 AND closed_at IS NULL)
 OR EXISTS(SELECT 1 FROM pixels.connection_observations WHERE node_id=$1 AND state IN ('active','unknown'))
 OR EXISTS(SELECT 1 FROM pixels.file_transfers WHERE node_id=$1 AND state IN ('active','unknown')) AS "busy!"
FROM pixels.nodes WHERE id=$1
