INSERT INTO pixels.resource_session_retirements
(session_id,node_id,challenge_id,session_revision,node_generation,control_epoch,deadline)
VALUES($1,$2,$3,$4,$5,$6,clock_timestamp()+interval '30 seconds')
ON CONFLICT(session_id) DO UPDATE SET challenge_id=EXCLUDED.challenge_id,session_revision=EXCLUDED.session_revision,
node_generation=EXCLUDED.node_generation,control_epoch=EXCLUDED.control_epoch,deadline=EXCLUDED.deadline,completed_at=NULL
RETURNING deadline
