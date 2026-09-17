WITH moved AS (
UPDATE pixels.instances SET state='reconcile_required',revision=revision+1
WHERE ended_at IS NULL AND state<>'reconcile_required' AND ($1::uuid IS NULL OR node_id=$1)
RETURNING id,revision)
INSERT INTO pixels.instance_events(id,instance_id,revision,kind)
SELECT gen_random_uuid(),id,revision,'reconcile_required' FROM moved
