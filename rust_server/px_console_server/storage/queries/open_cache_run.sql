WITH created AS (INSERT INTO pixels.cache_runs(id,root_id,control_epoch,byte_limit,max_downloads,ttl_seconds)
 VALUES($1,$2,$3,$4,$5,$6) RETURNING id)
INSERT INTO pixels.cache_runtime(singleton,run_id) SELECT true,id FROM created ON CONFLICT(singleton) DO UPDATE SET run_id=excluded.run_id
RETURNING run_id
