INSERT INTO pixels.recordings
(id,node_id,source_id,session_id,file_name,size_bytes,modified_at,codec,metadata_hash,reported_present,node_generation,control_epoch,source_sequence,source_sha256)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,true,$10,$11,$12,$13) RETURNING id,node_id,session_id,file_name,size_bytes,modified_at,codec,metadata_hash,reported_present,node_generation,control_epoch,source_sequence,revision,created_at,observed_at
