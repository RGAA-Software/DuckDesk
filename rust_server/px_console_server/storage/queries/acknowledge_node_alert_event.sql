UPDATE pixels.node_telemetry_alert_events
SET state='acknowledged',acknowledged_by=$3,acknowledged_at=clock_timestamp(),updated_at=clock_timestamp(),revision=revision+1
WHERE id=$1 AND revision=$2 AND state='active'
RETURNING id,node_id,metric,resource_key,resource_name,severity,state,threshold_per_mille,first_value_per_mille,
 latest_value_per_mille,peak_value_per_mille,occurrence_count,first_sampled_at,last_sampled_at,created_at,updated_at,
 acknowledged_by,acknowledged_at,recovered_at,revision
