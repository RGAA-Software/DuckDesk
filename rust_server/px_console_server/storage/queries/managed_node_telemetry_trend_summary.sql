SELECT
    node.id AS node_id,
    observation.evaluated_at AS "evaluated_at!",
    latest.received_at AS "latest_received_at?",
    CASE
        WHEN latest.received_at IS NULL THEN NULL
        ELSE GREATEST(0, EXTRACT(EPOCH FROM observation.evaluated_at - latest.received_at)::bigint)
    END AS "latest_age_seconds?",
    COALESCE(latest.received_at <= observation.evaluated_at - INTERVAL '30 seconds', TRUE) AS "stale!"
FROM pixels.nodes AS node
CROSS JOIN LATERAL (SELECT clock_timestamp() AS evaluated_at) AS observation
LEFT JOIN LATERAL (
    SELECT history.received_at
    FROM pixels.node_telemetry_history AS history
    WHERE history.node_id = node.id
    ORDER BY history.received_at DESC, history.node_generation DESC, history.report_sequence DESC
    LIMIT 1
) AS latest ON TRUE
WHERE node.id = $1
  AND node.deleted_at IS NULL
