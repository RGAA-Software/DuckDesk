SELECT COALESCE(sum(size_bytes),0)::bigint AS "reserved!",
count(*) FILTER(WHERE state='fetching' AND run_id=$2 AND lease_until>clock_timestamp() AND deadline>clock_timestamp()) AS "active!"
FROM pixels.cache_blobs WHERE root_id=$1 AND state<>'deleted'
