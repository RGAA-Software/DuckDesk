UPDATE pixels.instance_commands SET state='claimed',instance_revision=$2,attempts=attempts+1,lease_id=$3,
lease_until=LEAST(deadline,clock_timestamp()+interval '15 seconds') WHERE id=$1
RETURNING lease_until AS "lease_until!"
