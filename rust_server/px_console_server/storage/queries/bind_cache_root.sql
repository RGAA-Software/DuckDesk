INSERT INTO pixels.cache_roots(singleton,id,deployment_id) VALUES(true,$1,$2) ON CONFLICT(singleton) DO NOTHING
