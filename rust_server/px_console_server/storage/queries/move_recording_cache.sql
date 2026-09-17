UPDATE pixels.recording_cache SET active_blob_id=$2,revision=revision+1,updated_at=clock_timestamp()
WHERE recording_id=$1 RETURNING recording_id,active_blob_id,pinned,revision,updated_at
