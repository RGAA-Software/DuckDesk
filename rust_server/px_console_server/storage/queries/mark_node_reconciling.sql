UPDATE pixels.nodes SET state='reconciling' WHERE id=$1 AND connection_hash IS NOT NULL
