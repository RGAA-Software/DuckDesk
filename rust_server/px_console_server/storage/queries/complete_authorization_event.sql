UPDATE pixels.authorization_outbox SET delivered_at=clock_timestamp(),lease_id=NULL,lease_until=NULL,last_error=NULL
WHERE id=$1 AND lease_id=$2 AND lease_until>clock_timestamp() AND delivered_at IS NULL
