SELECT disconnect_grace_seconds,kind,executable_path,arguments,entry_url,codec,bitrate_kbps,gpu_key,gpu_inventory_revision FROM pixels.instances WHERE id=$1
