SELECT c.recording_id,c.pinned,c.revision,c.updated_at,r.size_bytes,COALESCE(b.received_bytes,0) AS "received_bytes!",
CASE WHEN b.id IS NULL THEN 'missing' WHEN b.state='published' AND b.verified_run=$1 THEN 'ready'
WHEN b.state='published' THEN 'verifying' WHEN b.state='fetching' AND b.run_id=$1 AND b.lease_until>clock_timestamp()
AND b.deadline>clock_timestamp() THEN 'fetching' ELSE 'retry_required' END AS "state!"
FROM pixels.recording_cache c JOIN pixels.recordings r ON r.id=c.recording_id LEFT JOIN pixels.cache_blobs b ON b.id=c.active_blob_id
WHERE ($2::uuid IS NULL OR c.recording_id>$2) ORDER BY c.recording_id LIMIT $3
