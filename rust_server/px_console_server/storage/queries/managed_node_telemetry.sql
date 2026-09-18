SELECT node_id,node_generation,report_sequence,probe_state,sampled_at,received_at,logical_processors,
 cpu_utilization_per_mille,memory_total_bytes,memory_available_bytes,disk_total_bytes,disk_free_bytes,gpu_inventory_revision
FROM pixels.node_telemetry_latest WHERE node_id=ANY($1::uuid[]) ORDER BY node_id
