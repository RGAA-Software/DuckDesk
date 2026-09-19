SELECT node_id,stable_key,inventory_revision,name,runtime_binding_ready,dedicated_memory_bytes,used_memory_bytes,
 utilization_per_mille,encoder_utilization_per_mille,sampled_at,received_at
FROM pixels.node_gpu_latest WHERE node_id=ANY($1::uuid[]) ORDER BY node_id,stable_key
