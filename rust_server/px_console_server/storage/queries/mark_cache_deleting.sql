UPDATE pixels.cache_blobs SET state='deleting',verified_run=NULL,updated_at=clock_timestamp() WHERE id=$1
