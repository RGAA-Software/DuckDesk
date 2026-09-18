SELECT breach_samples,recovery_samples,open_event_id
FROM pixels.node_telemetry_alert_conditions
WHERE node_id=$1 AND metric=$2 AND resource_key=$3
FOR UPDATE
