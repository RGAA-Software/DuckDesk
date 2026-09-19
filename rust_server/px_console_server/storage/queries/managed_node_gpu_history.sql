SELECT history.node_id,history.node_generation,history.report_sequence,history.stable_key,
 history.inventory_revision,history.name,history.runtime_binding_ready,history.dedicated_memory_bytes,history.used_memory_bytes,
 history.utilization_per_mille,history.encoder_utilization_per_mille,history.sampled_at,history.received_at
FROM pixels.node_gpu_history AS history
JOIN unnest($2::bigint[],$3::bigint[]) AS sample(node_generation,report_sequence)
 ON sample.node_generation=history.node_generation AND sample.report_sequence=history.report_sequence
WHERE history.node_id=$1
ORDER BY history.received_at DESC,history.node_generation DESC,history.report_sequence DESC,history.stable_key
