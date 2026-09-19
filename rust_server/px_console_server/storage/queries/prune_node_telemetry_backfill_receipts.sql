WITH expired AS (
 SELECT node_id,sample_id
 FROM pixels.node_telemetry_backfill_receipts
 WHERE received_at<clock_timestamp()-INTERVAL '8 days'
 ORDER BY received_at,node_id,sample_id
 LIMIT 5000
)
DELETE FROM pixels.node_telemetry_backfill_receipts AS receipt
USING expired
WHERE receipt.node_id=expired.node_id AND receipt.sample_id=expired.sample_id
