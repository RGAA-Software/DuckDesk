SELECT EXISTS(SELECT 1 FROM pixels.cache_roots WHERE id=$1 AND deployment_id=$2) AS "matches!"
