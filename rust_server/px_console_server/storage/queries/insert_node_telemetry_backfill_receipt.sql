WITH existing AS (
 SELECT sampled_at,payload_sha256,received_at
 FROM pixels.node_telemetry_backfill_receipts
 WHERE node_id=$1 AND sample_id=$2
), inserted AS (
 INSERT INTO pixels.node_telemetry_backfill_receipts(node_id,sample_id,sampled_at,payload_sha256)
 SELECT $1,$2,$3,$4 WHERE NOT EXISTS(SELECT 1 FROM existing)
 ON CONFLICT(node_id,sample_id) DO NOTHING
 RETURNING received_at
)
SELECT
 COALESCE((SELECT received_at FROM inserted),(SELECT received_at FROM existing)) AS "received_at!",
 EXISTS(SELECT 1 FROM inserted) AS "inserted!",
 COALESCE((SELECT sampled_at=$3 AND payload_sha256=$4 FROM existing),true) AS "exact!"
