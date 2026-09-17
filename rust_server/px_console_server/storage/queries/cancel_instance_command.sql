UPDATE pixels.instance_commands SET state='cancelled',lease_id=NULL,lease_until=NULL,completed_at=clock_timestamp()
WHERE id=$1 AND state IN ('pending','claimed')
