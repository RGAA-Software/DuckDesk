UPDATE pixels.nodes SET connection_hash=NULL,state='offline',reconciliation_id=NULL,reconciliation_deadline=NULL,generation=generation+1
WHERE device_id=$1 AND deleted_at IS NULL
RETURNING id
