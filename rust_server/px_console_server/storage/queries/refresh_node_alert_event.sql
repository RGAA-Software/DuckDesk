UPDATE pixels.node_telemetry_alert_events
SET resource_name=$2,severity=CASE WHEN severity='critical' OR $3='critical' THEN 'critical' ELSE 'warning' END,
 threshold_per_mille=CASE WHEN severity='critical' OR $3='critical' THEN $4 ELSE threshold_per_mille END,
 latest_value_per_mille=$5,peak_value_per_mille=GREATEST(peak_value_per_mille,$5),occurrence_count=occurrence_count+1,
 last_sampled_at=$6,updated_at=clock_timestamp(),revision=revision+1
WHERE id=$1 AND state<>'recovered'
