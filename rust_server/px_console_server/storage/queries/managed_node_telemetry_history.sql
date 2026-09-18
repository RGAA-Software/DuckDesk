SELECT node_id,node_generation,report_sequence,probe_state,sampled_at,received_at,logical_processors,
 cpu_utilization_per_mille,memory_total_bytes,memory_available_bytes,disk_total_bytes,disk_free_bytes,
 gpu_inventory_revision
FROM pixels.node_telemetry_history
WHERE node_id=$1 AND (
 $2::timestamptz IS NULL OR
 (received_at,node_generation,report_sequence)<($2,$3,$4)
)
ORDER BY received_at DESC,node_generation DESC,report_sequence DESC
LIMIT $5
