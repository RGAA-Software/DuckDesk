UPDATE pixels.recording_cache SET last_access_at=clock_timestamp() WHERE recording_id=$1
