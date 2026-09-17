INSERT INTO pixels.file_transfers
(id,node_id,session_id,node_generation,control_epoch,request_id,request_hash,direction,file_name,total_bytes,expected_sha256)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) RETURNING id,session_id,node_id,node_generation,control_epoch,request_hash,expected_sha256,report_hash,direction,file_name,total_bytes,transferred_bytes,state,reason,sequence,revision,created_at,updated_at,ended_at
