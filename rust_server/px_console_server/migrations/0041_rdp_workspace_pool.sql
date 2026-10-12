-- Each deployment owns persistent user desktops numbered 1..capacity. Existing
-- workspaces and instance history retain slot 1 and their unchanged credentials.
DO $$
DECLARE singleton_check_name text;
BEGIN
 SELECT conname INTO STRICT singleton_check_name
 FROM pg_constraint
 WHERE conrelid='pixels.application_deployments'::regclass AND contype='c'
 AND pg_get_constraintdef(oid) LIKE '%kind%rdp%capacity%';
 EXECUTE format('ALTER TABLE pixels.application_deployments DROP CONSTRAINT %I', singleton_check_name);
END $$;

ALTER TABLE pixels.rdp_workspaces
 ADD COLUMN workspace_slot integer NOT NULL DEFAULT 1 CHECK(workspace_slot BETWEEN 1 AND 64),
 DROP CONSTRAINT rdp_workspaces_application_id_node_id_key,
 ADD CONSTRAINT workspaces_application_node_slot UNIQUE(application_id,node_id,workspace_slot);

ALTER TABLE pixels.instances ADD COLUMN rdp_workspace_slot integer;
UPDATE pixels.instances SET rdp_workspace_slot=1 WHERE kind='rdp';
ALTER TABLE pixels.instances ADD CONSTRAINT instances_workspace_slot_kind CHECK(
 (kind='rdp' AND rdp_workspace_slot IS NOT NULL AND rdp_workspace_slot BETWEEN 1 AND 64)
 OR (kind<>'rdp' AND rdp_workspace_slot IS NULL)
);
DROP INDEX pixels.instances_rdp_busy;
CREATE UNIQUE INDEX instances_rdp_workspace_busy
 ON pixels.instances(application_id,node_id,rdp_workspace_slot)
 WHERE kind='rdp' AND ended_at IS NULL;
