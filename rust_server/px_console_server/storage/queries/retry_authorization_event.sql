UPDATE pixels.authorization_outbox SET available_at=clock_timestamp()+make_interval(secs=>$3),last_error=$4,lease_id=NULL,lease_until=NULL
WHERE id=$1 AND lease_id=$2 AND lease_until>clock_timestamp() AND delivered_at IS NULL
