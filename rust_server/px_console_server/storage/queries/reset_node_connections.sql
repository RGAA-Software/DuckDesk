UPDATE pixels.nodes SET connection_hash=NULL,state='offline',reconciliation_id=NULL,reconciliation_deadline=NULL,generation=generation+1,report_sequence=0,last_seen=NULL
WHERE connection_hash IS NOT NULL
