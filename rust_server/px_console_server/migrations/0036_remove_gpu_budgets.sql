-- Manual per-instance GPU budgets and resource-pressure admission limits are retired.
-- Preserve concrete adapter identity and existing instance/deployment lifecycle state.
ALTER TABLE pixels.application_deployments DROP CONSTRAINT deployments_gpu_profile;
ALTER TABLE pixels.instances DROP CONSTRAINT instances_gpu_reservation;
ALTER TABLE pixels.application_deployments
 DROP COLUMN gpu_memory_bytes,
 DROP COLUMN gpu_compute_per_mille,
 DROP COLUMN gpu_encoder_per_mille,
 DROP COLUMN gpu_memory_reserve_bytes,
 DROP COLUMN gpu_compute_limit_per_mille,
 DROP COLUMN gpu_encoder_limit_per_mille,
 ADD CONSTRAINT deployments_gpu_binding CHECK (kind <> 'rdp' OR gpu_key IS NULL);
ALTER TABLE pixels.instances
 DROP COLUMN gpu_memory_reservation_bytes,
 DROP COLUMN gpu_compute_reservation_per_mille,
 DROP COLUMN gpu_encoder_reservation_per_mille,
 DROP COLUMN gpu_memory_reserve_bytes,
 DROP COLUMN gpu_compute_limit_per_mille,
 DROP COLUMN gpu_encoder_limit_per_mille,
 ADD CONSTRAINT instances_gpu_binding CHECK (
  (kind = 'rdp' AND gpu_key IS NULL AND gpu_inventory_revision IS NULL) OR
  (kind IN ('game_hook', 'webview') AND gpu_key IS NOT NULL AND gpu_inventory_revision IS NOT NULL AND gpu_inventory_revision > 0)
 );
