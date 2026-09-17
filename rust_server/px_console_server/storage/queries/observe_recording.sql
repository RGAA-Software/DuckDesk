UPDATE pixels.recordings SET reported_present=$2,node_generation=$3,control_epoch=$4,source_sequence=$5,
revision=revision+1,observed_at=clock_timestamp() WHERE id=$1 RETURNING id,node_id,session_id,file_name,size_bytes,modified_at,codec,metadata_hash,reported_present,node_generation,control_epoch,source_sequence,revision,created_at,observed_at
