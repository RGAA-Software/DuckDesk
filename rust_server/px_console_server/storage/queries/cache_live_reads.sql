SELECT count(*) AS "count!" FROM pixels.cache_read_leases WHERE closed_at IS NULL AND expires_at>clock_timestamp()
AND run_id=$1 AND ($2::uuid IS NULL OR blob_id=$2) AND ($3::uuid IS NULL OR login_session_id=$3)
