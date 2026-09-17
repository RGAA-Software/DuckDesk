SELECT EXISTS(SELECT 1 FROM pixels.cache_runtime c JOIN pixels.cache_runs r ON r.id=c.run_id
WHERE r.id=$1 AND r.root_id=$2 AND r.control_epoch=$3 AND r.control_epoch=(SELECT epoch FROM pixels.control_runtime)) AS "current!"
