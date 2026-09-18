INSERT INTO pixels.node_gpu_history(
 node_id,node_generation,report_sequence,stable_key,inventory_revision,name,dedicated_memory_bytes,
 used_memory_bytes,utilization_per_mille,encoder_utilization_per_mille,sampled_at,received_at
) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12)
