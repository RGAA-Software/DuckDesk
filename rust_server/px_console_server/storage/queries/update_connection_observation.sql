UPDATE pixels.connection_observations SET state=$2,reason=$3,sent_bytes=$4,received_bytes=$5,elapsed_ms=$6,
sequence=$7,report_hash=$8,revision=revision+1,updated_at=clock_timestamp(),
ended_at=CASE WHEN $2 IN ('closed','failed') THEN clock_timestamp() ELSE NULL END WHERE id=$1 RETURNING id,source_id,session_id,node_id,node_generation,control_epoch,kind,request_hash,state,reason,sent_bytes,received_bytes,elapsed_ms,sequence,report_hash,revision,created_at,updated_at,ended_at
