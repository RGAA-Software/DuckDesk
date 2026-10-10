-- A file-only connection reuses device authorization without reserving a controller seat.
ALTER TABLE pixels.resource_sessions DROP CONSTRAINT resource_sessions_access_role_check;
ALTER TABLE pixels.resource_sessions ADD CONSTRAINT resource_sessions_access_role_check
 CHECK (access_role IN ('controller','observer','file_transfer'));
ALTER TABLE pixels.resource_sessions ADD CONSTRAINT resource_sessions_file_target_check
 CHECK (access_role <> 'file_transfer' OR (target_kind='desktop' AND client_type='panel' AND owner_user IS NOT NULL));
