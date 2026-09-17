UPDATE pixels.nodes SET state='reconciling',reconciliation_id=$2,reconciliation_deadline=clock_timestamp()+interval '30 seconds'
WHERE id=$1 RETURNING reconciliation_deadline AS "deadline!"
