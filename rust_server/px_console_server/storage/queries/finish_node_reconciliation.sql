UPDATE pixels.nodes SET state='ready',reconciliation_id=NULL,reconciliation_deadline=NULL
WHERE id=$1 AND reconciliation_id=$2 AND reconciliation_deadline>clock_timestamp() AND state='reconciling'
