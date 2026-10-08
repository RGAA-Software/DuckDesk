-- Persist one exact executable path throughout scheduling and launch snapshots.
-- Refuse ambiguous installed applications instead of choosing a node's directory.
DO $$
BEGIN
 IF EXISTS (
  SELECT 1 FROM pixels.applications application
  LEFT JOIN pixels.application_deployments deployment ON deployment.application_id=application.id
  WHERE application.kind='game_hook' AND application.deleted_at IS NULL
  GROUP BY application.id
  HAVING count(DISTINCT deployment.install_root)<>1
 ) THEN
  RAISE EXCEPTION 'absolute executable upgrade requires one unambiguous installation directory per existing game application';
 END IF;
END $$;

UPDATE pixels.applications application
SET executable_relative=rtrim(directory.install_root, E'\\') || E'\\' || application.executable_relative
FROM (
 SELECT application_id,min(install_root) AS install_root
 FROM pixels.application_deployments WHERE kind='game_hook'
 GROUP BY application_id HAVING count(DISTINCT install_root)=1
) directory
WHERE application.id=directory.application_id AND application.kind='game_hook';

UPDATE pixels.instances
SET executable_relative=rtrim(install_root, E'\\') || E'\\' || executable_relative
WHERE kind='game_hook';

ALTER TABLE pixels.applications RENAME COLUMN executable_relative TO executable_path;
ALTER TABLE pixels.instances RENAME COLUMN executable_relative TO executable_path;
ALTER TABLE pixels.instances DROP COLUMN install_root;
ALTER TABLE pixels.application_deployments DROP COLUMN install_root;

ALTER TABLE pixels.applications ADD CONSTRAINT applications_absolute_executable
 CHECK (kind<>'game_hook' OR deleted_at IS NOT NULL OR executable_path ~ '^[A-Za-z]:\\');
ALTER TABLE pixels.instances ADD CONSTRAINT instances_launch_shape
 CHECK ((kind='game_hook' AND executable_path IS NOT NULL AND arguments IS NOT NULL AND entry_url IS NULL AND codec IS NOT NULL AND bitrate_kbps IS NOT NULL)
 OR (kind='webview' AND executable_path IS NULL AND arguments IS NULL AND entry_url IS NOT NULL AND codec IS NOT NULL AND bitrate_kbps IS NOT NULL)
 OR (kind='rdp' AND executable_path IS NULL AND arguments IS NULL AND entry_url IS NULL AND codec IS NULL AND bitrate_kbps IS NULL));
