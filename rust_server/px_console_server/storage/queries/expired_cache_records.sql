SELECT c.recording_id FROM pixels.recording_cache c JOIN pixels.cache_blobs b ON b.id=c.active_blob_id
WHERE b.state='fetching' AND (b.run_id<>$1 OR b.lease_until<=clock_timestamp() OR b.deadline<=clock_timestamp())
ORDER BY b.lease_until,c.recording_id LIMIT $2
