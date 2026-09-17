UPDATE pixels.cache_blobs SET state='deleted',updated_at=clock_timestamp() WHERE id=$1
