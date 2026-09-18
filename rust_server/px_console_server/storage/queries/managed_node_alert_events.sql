SELECT id,node_id,metric,resource_key,resource_name,severity,state,threshold_per_mille,first_value_per_mille,
 latest_value_per_mille,peak_value_per_mille,occurrence_count,first_sampled_at,last_sampled_at,created_at,updated_at,
 acknowledged_by,acknowledged_at,recovered_at,revision
FROM pixels.node_telemetry_alert_events
WHERE ($1::uuid IS NULL OR node_id=$1)
 AND ($2::text IS NULL OR metric=$2)
 AND ($3::text IS NULL OR severity=$3)
 AND ($4::text IS NULL OR state=$4)
 AND ($5::timestamptz IS NULL OR (updated_at,id)<($5,$6))
ORDER BY updated_at DESC,id DESC
LIMIT $7
