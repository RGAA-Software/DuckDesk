WITH expired AS (
 SELECT id FROM pixels.node_telemetry_alert_events
 WHERE state='recovered' AND recovered_at<clock_timestamp()-INTERVAL '180 days'
 ORDER BY recovered_at,id
 LIMIT 5000
)
DELETE FROM pixels.node_telemetry_alert_events event
USING expired
WHERE event.id=expired.id
