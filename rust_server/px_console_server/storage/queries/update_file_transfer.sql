UPDATE pixels.file_transfers SET sequence=$2,transferred_bytes=$3,state=$4,reason=$5,
received_sha256=$6,report_hash=$7,revision=revision+1,updated_at=clock_timestamp(),
ended_at=CASE WHEN $4 IN ('completed','failed','cancelled') THEN clock_timestamp() ELSE NULL END
WHERE id=$1 AND state='active' RETURNING id,session_id,node_id,node_generation,control_epoch,request_hash,expected_sha256,report_hash,direction,file_name,total_bytes,transferred_bytes,state,reason,sequence,revision,created_at,updated_at,ended_at
