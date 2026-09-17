INSERT INTO pixels.connection_observations(id,source_id,session_id,node_id,node_generation,control_epoch,kind,request_hash)
VALUES($1,$2,$3,$4,$5,$6,$7,$8) RETURNING id,source_id,session_id,node_id,node_generation,control_epoch,kind,request_hash,state,reason,sent_bytes,received_bytes,elapsed_ms,sequence,report_hash,revision,created_at,updated_at,ended_at
