SELECT EXISTS(SELECT 1 FROM pixels.nodes WHERE id=$1 AND reconciliation_id=$2
AND reconciliation_deadline>clock_timestamp() AND state='reconciling') AS "current!"
