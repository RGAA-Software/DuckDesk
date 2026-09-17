WITH moved AS (
UPDATE pixels.file_transfers SET state='unknown',reason=CASE WHEN $2::uuid IS NULL THEN 'control_lost' ELSE 'frontend_closed' END,
revision=revision+1,updated_at=clock_timestamp()
WHERE state='active' AND ($1::uuid IS NULL OR node_id=$1) AND ($2::uuid IS NULL OR session_id=$2)
RETURNING id,revision,sequence,transferred_bytes)
INSERT INTO pixels.file_transfer_events(id,transfer_id,revision,sequence,transferred_bytes,kind)
SELECT gen_random_uuid(),id,revision,sequence,transferred_bytes,'unknown' FROM moved
