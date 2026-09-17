SELECT
 EXISTS(SELECT 1 FROM pixels.nodes WHERE id=$1 AND NOT draining)
 AND NOT EXISTS(SELECT 1 FROM pixels.resource_sessions WHERE closed_at IS NULL AND access_role='controller' AND $4='controller'
 AND (device_id=$2 OR instance_id=$3))
 AND (SELECT count(*) FROM pixels.resource_sessions WHERE node_id=$1 AND closed_at IS NULL)<128
 AND (SELECT count(*) FROM pixels.resource_sessions WHERE closed_at IS NULL AND (device_id=$2 OR instance_id=$3))<32
 AS "available!"
