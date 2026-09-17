SELECT NOT b.pinned AND (b.state IN ('abandoned','deleting')
 OR (b.state='fetching' AND (b.run_id<>$2 OR b.lease_until<=clock_timestamp() OR b.deadline<=clock_timestamp()))
 OR (b.state='published' AND NOT c.pinned AND ($4 OR c.last_access_at<clock_timestamp()-make_interval(secs=>$3)))) AS "eligible!"
FROM pixels.cache_blobs b JOIN pixels.recording_cache c ON c.recording_id=b.recording_id WHERE b.id=$1
