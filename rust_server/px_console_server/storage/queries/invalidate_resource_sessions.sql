WITH moved AS (
UPDATE pixels.resource_sessions SET state='reconcile_required',revision=revision+1,descriptor_hash=NULL,descriptor_expires_at=NULL
WHERE closed_at IS NULL AND state<>'reconcile_required' AND ($1::uuid IS NULL OR node_id=$1)
RETURNING id,revision)
INSERT INTO pixels.resource_session_events(id,session_id,revision,kind)
SELECT gen_random_uuid(),id,revision,'reconcile_required' FROM moved
