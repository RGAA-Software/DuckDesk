UPDATE pixels.saved_connections SET name=$2,video_bitrate_bps=$3,video_fps=$4,audio_enabled=$5,clipboard_enabled=$6,
view_only=$7,maximize=$8,split_windows=$9,prefer_peer_to_peer=$10,audio_capture=$11,background_rgb=$12,
revision=revision+1,updated_at=clock_timestamp() WHERE id=$1 RETURNING id,owner_id,client_type,request_id,request_hash,name,device_id,application_id,video_bitrate_bps,video_fps,audio_enabled,clipboard_enabled,view_only,maximize,split_windows,prefer_peer_to_peer,audio_capture,background_rgb,revision,created_at,updated_at,deleted_at
