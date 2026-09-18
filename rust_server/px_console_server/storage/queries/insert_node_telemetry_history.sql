INSERT INTO pixels.node_telemetry_history(
 node_id,node_generation,report_sequence,probe_state,sampled_at,received_at,logical_processors,
 cpu_utilization_per_mille,memory_total_bytes,memory_available_bytes,disk_total_bytes,disk_free_bytes,
 gpu_inventory_revision
) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13)
