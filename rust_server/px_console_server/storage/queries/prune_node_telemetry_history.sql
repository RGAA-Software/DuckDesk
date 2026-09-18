WITH expired AS (
 SELECT node_id,node_generation,report_sequence
 FROM pixels.node_telemetry_history
 WHERE received_at<clock_timestamp()-INTERVAL '7 days'
 ORDER BY received_at,node_generation,report_sequence
 LIMIT 5000
)
DELETE FROM pixels.node_telemetry_history AS history
USING expired
WHERE history.node_id=expired.node_id
 AND history.node_generation=expired.node_generation
 AND history.report_sequence=expired.report_sequence
