UPDATE pixels.cache_blobs SET state='abandoned',verified_run=NULL,updated_at=clock_timestamp() WHERE id=$1
