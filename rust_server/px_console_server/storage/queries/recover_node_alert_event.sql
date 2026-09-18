UPDATE pixels.node_telemetry_alert_events
SET state='recovered',latest_value_per_mille=$2,last_sampled_at=$3,recovered_at=clock_timestamp(),
 updated_at=clock_timestamp(),revision=revision+1
WHERE id=$1 AND state<>'recovered'
