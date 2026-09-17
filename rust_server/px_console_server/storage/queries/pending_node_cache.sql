SELECT c.recording_id FROM pixels.recording_cache c JOIN pixels.cache_blobs b ON b.id=c.active_blob_id
JOIN pixels.recordings r ON r.id=c.recording_id WHERE r.node_id=$1 AND b.run_id=$2 AND b.node_generation=$3
AND b.state='fetching' AND b.lease_until>clock_timestamp() AND b.deadline>clock_timestamp()
AND ($4::uuid IS NULL OR c.recording_id>$4) ORDER BY c.recording_id LIMIT $5
