UPDATE pixels.cache_read_leases SET closed_at=COALESCE(closed_at,clock_timestamp()) WHERE id=$1 AND run_id=$2 AND blob_id=$3
