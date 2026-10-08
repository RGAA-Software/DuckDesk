WITH raw_candidates AS (
 SELECT
  d.id AS deployment_id,
  d.node_id,
  COALESCE(g.stable_key,d.gpu_key) AS gpu_key,
  a.disabled AS application_disabled,
  a.deleted_at IS NOT NULL AS application_deleted,
  d.disabled AS deployment_disabled,
  n.disabled AS node_disabled,
  n.draining AS node_draining,
  n.deleted_at IS NOT NULL AS node_deleted,
  v.disabled AS device_disabled,
  v.deleted_at IS NOT NULL AS device_deleted,
  n.state,
  n.connection_hash IS NOT NULL AS node_connected,
  n.last_seen,
  n.control_epoch,
  n.public_host,
  n.report_sequence,
  n.generation,
  n.endpoint_revision,
  n.game_hook,
  n.webview,
  n.rdp,
  n.max_instances::bigint-node_usage.used AS node_slots,
  d.capacity::bigint-deployment_usage.used AS deployment_slots,
  available.port IS NOT NULL AS port_available,
  rdp_usage.used>0 AS rdp_workspace_busy,
  d.kind,
  d.revision AS deployment_revision,
  d.application_revision AS deployment_application_revision,
  a.revision AS application_revision,
  d.observed_state,
  d.observed_generation,
  d.observed_epoch,
  d.observed_endpoint_revision,
  t.node_id IS NOT NULL AND t.node_generation=n.generation AND t.report_sequence=n.report_sequence
   AND t.probe_state IN ('ready','partial') AND t.gpu_inventory_revision IS NOT NULL AS gpu_inventory_current,
  g.stable_key IS NOT NULL AS gpu_present,
  d.gpu_key IS NOT NULL AS gpu_pinned,
  g.runtime_binding_ready,
  g.dedicated_memory_bytes,
  g.used_memory_bytes,
  g.utilization_per_mille,
  g.encoder_utilization_per_mille,
  d.gpu_memory_bytes,
  d.gpu_compute_per_mille,
  d.gpu_encoder_per_mille,
  d.gpu_memory_reserve_bytes,
  d.gpu_compute_limit_per_mille,
  d.gpu_encoder_limit_per_mille,
  gpu_usage.running_memory,
  gpu_usage.pending_memory,
  gpu_usage.running_compute,
  gpu_usage.pending_compute,
  gpu_usage.running_encoder,
  gpu_usage.pending_encoder
 FROM pixels.application_deployments d
 JOIN pixels.applications a ON a.id=d.application_id
 JOIN pixels.nodes n ON n.id=d.node_id
 JOIN pixels.devices v ON v.id=n.device_id
 LEFT JOIN pixels.node_telemetry_latest t ON t.node_id=n.id
 LEFT JOIN pixels.node_gpu_latest g ON d.kind<>'rdp' AND g.node_id=n.id
  AND g.inventory_revision=t.gpu_inventory_revision AND (d.gpu_key IS NULL OR g.stable_key=d.gpu_key)
 CROSS JOIN LATERAL (
  SELECT count(*) AS used FROM pixels.instances i WHERE i.node_id=n.id AND i.ended_at IS NULL
 ) node_usage
 CROSS JOIN LATERAL (
  SELECT count(*) AS used FROM pixels.instances i WHERE i.deployment_id=d.id AND i.ended_at IS NULL
 ) deployment_usage
 LEFT JOIN LATERAL (
  SELECT p AS port FROM generate_series(n.application_port_start,n.application_port_end) p
  WHERE NOT EXISTS(
   SELECT 1 FROM pixels.instances i WHERE i.node_id=n.id AND i.port=p AND i.ended_at IS NULL
  ) ORDER BY p LIMIT 1
 ) available ON TRUE
 CROSS JOIN LATERAL (
  SELECT count(*) AS used FROM pixels.instances i
  WHERE i.application_id=a.id AND i.node_id=n.id AND i.ended_at IS NULL
 ) rdp_usage
 CROSS JOIN LATERAL (
  SELECT
   COALESCE(sum(i.gpu_memory_reservation_bytes) FILTER(WHERE i.state IN ('running','stopping','reconcile_required')),0)::bigint AS running_memory,
   COALESCE(sum(i.gpu_memory_reservation_bytes) FILTER(WHERE i.state IN ('reserved','starting')),0)::bigint AS pending_memory,
   COALESCE(sum(i.gpu_compute_reservation_per_mille) FILTER(WHERE i.state IN ('running','stopping','reconcile_required')),0)::bigint AS running_compute,
   COALESCE(sum(i.gpu_compute_reservation_per_mille) FILTER(WHERE i.state IN ('reserved','starting')),0)::bigint AS pending_compute,
   COALESCE(sum(i.gpu_encoder_reservation_per_mille) FILTER(WHERE i.state IN ('running','stopping','reconcile_required')),0)::bigint AS running_encoder,
   COALESCE(sum(i.gpu_encoder_reservation_per_mille) FILTER(WHERE i.state IN ('reserved','starting')),0)::bigint AS pending_encoder
  FROM pixels.instances i WHERE i.node_id=n.id AND i.gpu_key=g.stable_key AND i.ended_at IS NULL
 ) gpu_usage
 WHERE a.id=$1 AND ($2::uuid IS NULL OR d.id=$2)
), measured_candidates AS (
 SELECT raw_candidates.*,
  CASE WHEN kind='rdp' OR dedicated_memory_bytes IS NULL OR used_memory_bytes IS NULL THEN NULL ELSE
   dedicated_memory_bytes-gpu_memory_reserve_bytes-
   GREATEST(used_memory_bytes,running_memory)-pending_memory-gpu_memory_bytes END AS gpu_memory_headroom_bytes,
  CASE WHEN kind='rdp' OR utilization_per_mille IS NULL THEN NULL ELSE
   gpu_compute_limit_per_mille::bigint-GREATEST(utilization_per_mille::bigint,running_compute)-pending_compute-gpu_compute_per_mille END
   AS gpu_compute_headroom_per_mille,
  CASE WHEN kind='rdp' OR encoder_utilization_per_mille IS NULL THEN NULL ELSE
   gpu_encoder_limit_per_mille::bigint-GREATEST(encoder_utilization_per_mille::bigint,running_encoder)-pending_encoder-gpu_encoder_per_mille END
   AS gpu_encoder_headroom_per_mille,
  CASE WHEN kind='rdp' THEN 0::bigint
   WHEN dedicated_memory_bytes IS NULL OR used_memory_bytes IS NULL OR utilization_per_mille IS NULL OR encoder_utilization_per_mille IS NULL
    THEN NULL
   ELSE GREATEST(
    ((GREATEST(used_memory_bytes,running_memory)+pending_memory+gpu_memory_bytes)*1000)/
     NULLIF(dedicated_memory_bytes-gpu_memory_reserve_bytes,0),
    ((GREATEST(utilization_per_mille::bigint,running_compute)+pending_compute+gpu_compute_per_mille)*1000)/
     NULLIF(gpu_compute_limit_per_mille,0),
    ((GREATEST(encoder_utilization_per_mille::bigint,running_encoder)+pending_encoder+gpu_encoder_per_mille)*1000)/
     NULLIF(gpu_encoder_limit_per_mille,0))
  END AS dominant_pressure_per_mille,
  CASE WHEN kind='rdp' THEN 0::bigint
   WHEN dedicated_memory_bytes IS NULL OR used_memory_bytes IS NULL OR utilization_per_mille IS NULL OR encoder_utilization_per_mille IS NULL
    THEN NULL
   ELSE (
    ((GREATEST(used_memory_bytes,running_memory)+pending_memory+gpu_memory_bytes)*1000)/
     NULLIF(dedicated_memory_bytes-gpu_memory_reserve_bytes,0)+
    ((GREATEST(utilization_per_mille::bigint,running_compute)+pending_compute+gpu_compute_per_mille)*1000)/
     NULLIF(gpu_compute_limit_per_mille,0)+
    ((GREATEST(encoder_utilization_per_mille::bigint,running_encoder)+pending_encoder+gpu_encoder_per_mille)*1000)/
     NULLIF(gpu_encoder_limit_per_mille,0))/3
  END AS average_pressure_per_mille
 FROM raw_candidates
), evaluated_candidates AS (
 SELECT measured_candidates.*,
  array_remove(ARRAY[
   CASE WHEN application_disabled THEN 'application_disabled' END,
   CASE WHEN application_deleted THEN 'application_deleted' END,
   CASE WHEN deployment_disabled THEN 'deployment_disabled' END,
   CASE WHEN node_disabled THEN 'node_disabled' END,
   CASE WHEN node_draining THEN 'node_draining' END,
   CASE WHEN node_deleted THEN 'node_deleted' END,
   CASE WHEN device_disabled THEN 'device_disabled' END,
   CASE WHEN device_deleted THEN 'device_deleted' END,
   CASE WHEN state<>'ready' THEN 'node_not_ready' END,
   CASE WHEN NOT node_connected THEN 'node_disconnected' END,
   CASE WHEN last_seen IS NULL OR last_seen<=clock_timestamp()-interval '30 seconds' THEN 'node_stale' END,
   CASE WHEN control_epoch IS DISTINCT FROM $3 OR $3<>(SELECT epoch FROM pixels.control_runtime) THEN 'control_epoch_mismatch' END,
   CASE WHEN public_host IS NULL THEN 'public_endpoint_missing' END,
   CASE WHEN report_sequence=0 THEN 'inventory_missing' END,
   CASE WHEN observed_state<>'ready' THEN 'deployment_not_ready' END,
   CASE WHEN observed_generation IS DISTINCT FROM generation THEN 'deployment_generation_mismatch' END,
   CASE WHEN observed_epoch IS DISTINCT FROM control_epoch THEN 'deployment_epoch_mismatch' END,
   CASE WHEN observed_endpoint_revision IS DISTINCT FROM endpoint_revision THEN 'deployment_endpoint_revision_mismatch' END,
   CASE WHEN deployment_application_revision<>application_revision THEN 'deployment_revision_mismatch' END,
   CASE WHEN NOT ((kind='game_hook' AND game_hook) OR (kind='webview' AND webview) OR (kind='rdp' AND rdp)) THEN 'capability_missing' END,
   CASE WHEN node_slots<=0 THEN 'node_capacity_exhausted' END,
   CASE WHEN deployment_slots<=0 THEN 'deployment_capacity_exhausted' END,
   CASE WHEN NOT port_available THEN 'port_unavailable' END,
   CASE WHEN kind='rdp' AND rdp_workspace_busy THEN 'rdp_workspace_busy' END,
   CASE WHEN kind<>'rdp' AND NOT gpu_inventory_current THEN 'gpu_inventory_unavailable' END,
   CASE WHEN kind<>'rdp' AND gpu_inventory_current AND gpu_pinned AND NOT gpu_present THEN 'pinned_gpu_missing' END,
   CASE WHEN kind<>'rdp' AND gpu_inventory_current AND NOT gpu_pinned AND NOT gpu_present THEN 'gpu_inventory_unavailable' END,
   CASE WHEN kind<>'rdp' AND gpu_inventory_current AND gpu_present AND NOT runtime_binding_ready THEN 'gpu_binding_unavailable' END,
   CASE WHEN kind<>'rdp' AND gpu_inventory_current AND gpu_present AND
    (dedicated_memory_bytes IS NULL OR used_memory_bytes IS NULL OR utilization_per_mille IS NULL OR encoder_utilization_per_mille IS NULL)
    THEN 'gpu_metrics_unknown' END,
   CASE WHEN kind<>'rdp' AND gpu_memory_headroom_bytes<0 THEN 'gpu_memory_exhausted' END,
   CASE WHEN kind<>'rdp' AND gpu_compute_headroom_per_mille<0 THEN 'gpu_compute_exhausted' END,
   CASE WHEN kind<>'rdp' AND gpu_encoder_headroom_per_mille<0 THEN 'gpu_encoder_exhausted' END
  ]::text[],NULL) AS rejection_reasons
 FROM measured_candidates
)
SELECT
 statement_timestamp() AS "evaluated_at!",
 deployment_id AS "deployment_id!",
 node_id AS "node_id!",
 gpu_key,
 cardinality(rejection_reasons)=0 AS "eligible!",
 dominant_pressure_per_mille,
 average_pressure_per_mille,
 node_slots AS "node_slots!",
 deployment_slots AS "deployment_slots!",
 gpu_memory_headroom_bytes,
 gpu_compute_headroom_per_mille,
 gpu_encoder_headroom_per_mille,
 rejection_reasons AS "rejection_reasons!"
FROM evaluated_candidates
ORDER BY cardinality(rejection_reasons)=0 DESC,dominant_pressure_per_mille NULLS LAST,
 average_pressure_per_mille NULLS LAST,node_slots DESC,deployment_slots DESC,node_id,deployment_id,gpu_key NULLS FIRST
