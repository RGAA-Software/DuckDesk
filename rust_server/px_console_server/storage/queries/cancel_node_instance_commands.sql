UPDATE pixels.instance_commands SET state='cancelled',lease_id=NULL,lease_until=NULL,completed_at=clock_timestamp()
WHERE state IN ('pending','claimed') AND ($1::uuid IS NULL OR node_id=$1)
