UPDATE pixels.saved_connections SET deleted_at=clock_timestamp(),updated_at=clock_timestamp(),revision=revision+1
WHERE id=$1 RETURNING id,owner_id,client_type,request_id,request_hash,name,device_id,application_id,video_bitrate_bps,video_fps,audio_enabled,clipboard_enabled,view_only,maximize,split_windows,prefer_peer_to_peer,audio_capture,background_rgb,revision,created_at,updated_at,deleted_at
