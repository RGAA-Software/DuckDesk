INSERT INTO pixels.node_telemetry_alert_conditions(
 node_id,metric,resource_key,resource_name,breach_samples,recovery_samples,open_event_id,last_value_per_mille,last_sampled_at,
 node_generation,report_sequence
)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11)
ON CONFLICT(node_id,metric,resource_key) DO UPDATE SET
 resource_name=EXCLUDED.resource_name,breach_samples=EXCLUDED.breach_samples,recovery_samples=EXCLUDED.recovery_samples,
 open_event_id=EXCLUDED.open_event_id,last_value_per_mille=EXCLUDED.last_value_per_mille,last_sampled_at=EXCLUDED.last_sampled_at,
 node_generation=EXCLUDED.node_generation,report_sequence=EXCLUDED.report_sequence
