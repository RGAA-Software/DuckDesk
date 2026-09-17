WITH moved AS (UPDATE pixels.connection_observations SET state='unknown',
reason=CASE WHEN $2::uuid IS NULL THEN 'control_lost' ELSE 'frontend_closed' END,revision=revision+1,updated_at=clock_timestamp()
WHERE state='active' AND ($1::uuid IS NULL OR node_id=$1) AND ($2::uuid IS NULL OR session_id=$2)
RETURNING id,revision,sequence,state,sent_bytes,received_bytes,elapsed_ms)
INSERT INTO pixels.connection_observation_events(id,connection_id,revision,sequence,state,sent_bytes,received_bytes,elapsed_ms)
SELECT gen_random_uuid(),id,revision,sequence,state,sent_bytes,received_bytes,elapsed_ms FROM moved
