SELECT id,revision FROM pixels.devices WHERE enrollment_hash=$1 AND NOT disabled AND deleted_at IS NULL FOR SHARE
