DO $$
BEGIN
 IF EXISTS(SELECT 1 FROM pixels.application_deployments) OR EXISTS(SELECT 1 FROM pixels.instances) THEN
  RAISE EXCEPTION 'migration 0027 requires a fresh development database';
 END IF;
END $$;

ALTER TABLE pixels.node_gpu_latest
 ADD COLUMN runtime_binding_ready BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE pixels.node_gpu_history
 ADD COLUMN runtime_binding_ready BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE pixels.application_deployments
 ADD COLUMN gpu_memory_bytes BIGINT,
 ADD COLUMN gpu_compute_per_mille SMALLINT,
 ADD COLUMN gpu_encoder_per_mille SMALLINT,
 ADD COLUMN gpu_memory_reserve_bytes BIGINT,
 ADD COLUMN gpu_compute_limit_per_mille SMALLINT,
 ADD COLUMN gpu_encoder_limit_per_mille SMALLINT,
 ADD CONSTRAINT deployments_gpu_profile CHECK (
  (kind='rdp' AND gpu_key IS NULL AND gpu_memory_bytes IS NULL AND gpu_compute_per_mille IS NULL AND
   gpu_encoder_per_mille IS NULL AND gpu_memory_reserve_bytes IS NULL AND gpu_compute_limit_per_mille IS NULL AND
   gpu_encoder_limit_per_mille IS NULL)
  OR
  (kind IN ('game_hook','webview') AND gpu_memory_bytes>0 AND gpu_memory_bytes<=17592186044416 AND gpu_compute_per_mille>0 AND gpu_compute_per_mille<=1000 AND
   gpu_encoder_per_mille>0 AND gpu_encoder_per_mille<=1000 AND gpu_memory_reserve_bytes>=0 AND
   gpu_memory_reserve_bytes<=17592186044416 AND gpu_memory_bytes+gpu_memory_reserve_bytes<=17592186044416 AND
   gpu_compute_limit_per_mille>=gpu_compute_per_mille AND gpu_compute_limit_per_mille<=1000 AND
   gpu_encoder_limit_per_mille>=gpu_encoder_per_mille AND gpu_encoder_limit_per_mille<=1000)
 );

ALTER TABLE pixels.instances
 ADD COLUMN gpu_inventory_revision BIGINT,
 ADD COLUMN gpu_memory_reservation_bytes BIGINT,
 ADD COLUMN gpu_compute_reservation_per_mille SMALLINT,
 ADD COLUMN gpu_encoder_reservation_per_mille SMALLINT,
 ADD COLUMN gpu_memory_reserve_bytes BIGINT,
 ADD COLUMN gpu_compute_limit_per_mille SMALLINT,
 ADD COLUMN gpu_encoder_limit_per_mille SMALLINT,
 ADD CONSTRAINT instances_gpu_reservation CHECK (
  (kind='rdp' AND gpu_key IS NULL AND gpu_inventory_revision IS NULL AND gpu_memory_reservation_bytes IS NULL AND
   gpu_compute_reservation_per_mille IS NULL AND gpu_encoder_reservation_per_mille IS NULL AND gpu_memory_reserve_bytes IS NULL AND
   gpu_compute_limit_per_mille IS NULL AND gpu_encoder_limit_per_mille IS NULL)
  OR
  (kind IN ('game_hook','webview') AND gpu_key IS NOT NULL AND gpu_inventory_revision>0 AND gpu_memory_reservation_bytes>0 AND
   gpu_compute_reservation_per_mille>0 AND gpu_compute_reservation_per_mille<=1000 AND
   gpu_encoder_reservation_per_mille>0 AND gpu_encoder_reservation_per_mille<=1000 AND gpu_memory_reserve_bytes>=0 AND
   gpu_compute_limit_per_mille>=gpu_compute_reservation_per_mille AND gpu_compute_limit_per_mille<=1000 AND
   gpu_encoder_limit_per_mille>=gpu_encoder_reservation_per_mille AND gpu_encoder_limit_per_mille<=1000)
 );

CREATE INDEX instances_active_gpu ON pixels.instances(node_id,gpu_key,state)
 WHERE ended_at IS NULL AND gpu_key IS NOT NULL;

GRANT UPDATE(gpu_memory_bytes,gpu_compute_per_mille,gpu_encoder_per_mille,gpu_memory_reserve_bytes,
 gpu_compute_limit_per_mille,gpu_encoder_limit_per_mille) ON pixels.application_deployments TO pixels_console_runtime;
