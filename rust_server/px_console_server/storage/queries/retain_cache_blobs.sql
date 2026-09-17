UPDATE pixels.cache_blobs SET pinned=$2,updated_at=clock_timestamp()
WHERE recording_id=$1 AND (NOT $2 OR (id=$3 AND state='published'))
