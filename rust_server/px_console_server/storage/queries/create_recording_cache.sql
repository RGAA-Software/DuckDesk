INSERT INTO pixels.recording_cache(recording_id) VALUES($1) RETURNING recording_id,active_blob_id,pinned,revision,updated_at
