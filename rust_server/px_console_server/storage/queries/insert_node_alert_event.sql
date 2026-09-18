INSERT INTO pixels.node_telemetry_alert_events(
 id,node_id,metric,resource_key,resource_name,severity,state,threshold_per_mille,first_value_per_mille,
 latest_value_per_mille,peak_value_per_mille,first_sampled_at,last_sampled_at
)
VALUES($1,$2,$3,$4,$5,$6,'active',$7,$8,$8,$8,$9,$9)
