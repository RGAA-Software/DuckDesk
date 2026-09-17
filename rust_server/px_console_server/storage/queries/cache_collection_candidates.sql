SELECT b.id FROM pixels.cache_blobs b JOIN pixels.recording_cache c ON c.recording_id=b.recording_id
WHERE b.root_id=$1 AND NOT b.pinned AND (b.state IN ('abandoned','deleting')
 OR (b.state='fetching' AND (b.run_id<>$2 OR b.lease_until<=clock_timestamp() OR b.deadline<=clock_timestamp()))
 OR (b.state='published' AND NOT c.pinned AND c.last_access_at<clock_timestamp()-make_interval(secs=>$3)))
AND NOT EXISTS(SELECT 1 FROM pixels.cache_read_leases l WHERE l.blob_id=b.id AND l.run_id=$2 AND l.closed_at IS NULL AND l.expires_at>clock_timestamp())
AND ($4::uuid IS NULL OR b.id>$4) ORDER BY b.id LIMIT $5
