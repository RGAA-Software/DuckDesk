INSERT INTO pixels.node_telemetry_latest AS telemetry(
 node_id,node_generation,report_sequence,probe_state,sampled_at,logical_processors,cpu_utilization_per_mille,
 memory_total_bytes,memory_available_bytes,disk_total_bytes,disk_free_bytes,gpu_inventory_revision
) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12)
ON CONFLICT(node_id) DO UPDATE SET
 node_generation=excluded.node_generation,report_sequence=excluded.report_sequence,probe_state=excluded.probe_state,
 sampled_at=excluded.sampled_at,received_at=clock_timestamp(),logical_processors=excluded.logical_processors,
 cpu_utilization_per_mille=excluded.cpu_utilization_per_mille,memory_total_bytes=excluded.memory_total_bytes,
 memory_available_bytes=excluded.memory_available_bytes,disk_total_bytes=excluded.disk_total_bytes,
 disk_free_bytes=excluded.disk_free_bytes,gpu_inventory_revision=excluded.gpu_inventory_revision
WHERE (telemetry.node_generation,telemetry.report_sequence)<(excluded.node_generation,excluded.report_sequence)
RETURNING received_at
