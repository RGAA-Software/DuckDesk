SELECT recording_id,active_blob_id,pinned,revision,updated_at FROM pixels.recording_cache WHERE recording_id=$1 FOR UPDATE
