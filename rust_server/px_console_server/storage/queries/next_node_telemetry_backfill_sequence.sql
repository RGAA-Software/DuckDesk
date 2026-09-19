SELECT LEAST(COALESCE(MIN(report_sequence),0),0)-1 AS "sequence!"
FROM pixels.node_telemetry_history
WHERE node_id=$1 AND node_generation=$2
