WITH bounds AS (
    SELECT
        $1::uuid AS node_id,
        $2::timestamptz AS evaluated_at,
        $3::bigint AS window_seconds,
        $4::bigint AS bucket_seconds
),
aligned AS (
    SELECT
        node_id,
        evaluated_at,
        bucket_seconds,
        to_timestamp(
            floor(EXTRACT(EPOCH FROM evaluated_at - window_seconds * INTERVAL '1 second') / bucket_seconds)
            * bucket_seconds
        ) AS first_bucket,
        to_timestamp(floor(EXTRACT(EPOCH FROM evaluated_at) / bucket_seconds) * bucket_seconds) AS last_bucket
    FROM bounds
),
buckets AS (
    SELECT
        aligned.node_id,
        aligned.evaluated_at,
        aligned.bucket_seconds,
        generate_series(
            aligned.first_bucket,
            aligned.last_bucket,
            aligned.bucket_seconds * INTERVAL '1 second'
        ) AS bucket_start
    FROM aligned
),
gpu_samples AS (
    SELECT
        history.node_id,
        history.node_generation,
        history.report_sequence,
        max(history.utilization_per_mille) AS gpu_utilization_per_mille,
        max(history.encoder_utilization_per_mille) AS encoder_utilization_per_mille
    FROM pixels.node_gpu_history AS history
    CROSS JOIN aligned
    WHERE history.node_id = aligned.node_id
      AND history.sampled_at >= aligned.first_bucket
      AND history.sampled_at < aligned.last_bucket + aligned.bucket_seconds * INTERVAL '1 second'
    GROUP BY history.node_id, history.node_generation, history.report_sequence
),
samples AS (
    SELECT
        telemetry.node_id,
        to_timestamp(
            floor(EXTRACT(EPOCH FROM telemetry.sampled_at) / aligned.bucket_seconds)
            * aligned.bucket_seconds
        ) AS bucket_start,
        telemetry.cpu_utilization_per_mille,
        CASE
            WHEN telemetry.memory_total_bytes > 0 THEN round(
                (telemetry.memory_total_bytes - telemetry.memory_available_bytes)::numeric * 1000
                / telemetry.memory_total_bytes
            )::smallint
            ELSE NULL
        END AS memory_utilization_per_mille,
        CASE
            WHEN telemetry.disk_total_bytes > 0 THEN round(
                (telemetry.disk_total_bytes - telemetry.disk_free_bytes)::numeric * 1000
                / telemetry.disk_total_bytes
            )::smallint
            ELSE NULL
        END AS disk_utilization_per_mille,
        gpu_samples.gpu_utilization_per_mille,
        gpu_samples.encoder_utilization_per_mille
    FROM pixels.node_telemetry_history AS telemetry
    CROSS JOIN aligned
    LEFT JOIN gpu_samples
      ON gpu_samples.node_id = telemetry.node_id
     AND gpu_samples.node_generation = telemetry.node_generation
     AND gpu_samples.report_sequence = telemetry.report_sequence
    WHERE telemetry.node_id = aligned.node_id
      AND telemetry.sampled_at >= aligned.first_bucket
      AND telemetry.sampled_at < aligned.last_bucket + aligned.bucket_seconds * INTERVAL '1 second'
)
SELECT
    buckets.bucket_start AS "bucket_start!",
    count(samples.node_id)::bigint AS "sample_count!",
    count(samples.cpu_utilization_per_mille)::bigint AS "cpu_known_samples!",
    round(avg(samples.cpu_utilization_per_mille))::smallint AS cpu_average_per_mille,
    count(samples.memory_utilization_per_mille)::bigint AS "memory_known_samples!",
    round(avg(samples.memory_utilization_per_mille))::smallint AS memory_average_per_mille,
    count(samples.disk_utilization_per_mille)::bigint AS "disk_known_samples!",
    round(avg(samples.disk_utilization_per_mille))::smallint AS disk_average_per_mille,
    count(samples.gpu_utilization_per_mille)::bigint AS "gpu_known_samples!",
    round(avg(samples.gpu_utilization_per_mille))::smallint AS gpu_average_per_mille,
    count(samples.encoder_utilization_per_mille)::bigint AS "encoder_known_samples!",
    round(avg(samples.encoder_utilization_per_mille))::smallint AS encoder_average_per_mille
FROM buckets
LEFT JOIN samples
  ON samples.node_id = buckets.node_id
 AND samples.bucket_start = buckets.bucket_start
GROUP BY buckets.bucket_start
ORDER BY buckets.bucket_start
