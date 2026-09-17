UPDATE pixels.instance_commands SET state='completed',lease_id=NULL,lease_until=NULL,completed_at=clock_timestamp(),outcome=$2,completed_lease_id=$3 WHERE id=$1
