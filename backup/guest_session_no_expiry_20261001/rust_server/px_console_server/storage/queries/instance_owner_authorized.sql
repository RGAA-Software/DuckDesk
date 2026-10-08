SELECT EXISTS(
 SELECT 1 FROM pixels.instances i JOIN pixels.applications a ON a.id=i.application_id
 WHERE i.id=$1 AND i.ended_at IS NULL AND NOT a.disabled AND a.deleted_at IS NULL
 AND (
  (i.owner_user IS NOT NULL AND EXISTS(
   SELECT 1 FROM pixels.users u JOIN pixels.login_sessions s ON s.user_id=u.id
   WHERE u.id=i.owner_user AND s.id=i.login_session_id AND NOT u.disabled AND u.deleted_at IS NULL
   AND u.role IN ('user','admin') AND u.authorization_revision=i.owner_revision AND s.authorization_revision=i.owner_revision
   AND s.client_type=i.client_type AND s.revoked_at IS NULL AND s.expires_at>clock_timestamp() AND s.absolute_expires_at>clock_timestamp()
   AND (a.access_mode='public' OR EXISTS(
    SELECT 1 FROM pixels.group_app_grants ga JOIN pixels.user_groups g ON g.id=ga.group_id JOIN pixels.group_members gm ON gm.group_id=g.id
    WHERE ga.application_id=a.id AND gm.user_id=u.id AND g.deleted_at IS NULL))
  ))
  OR (i.owner_guest IS NOT NULL AND a.access_mode='public' AND EXISTS(
   SELECT 1 FROM pixels.guest_sessions g WHERE g.id=i.owner_guest AND g.revision=i.owner_revision
   AND g.client_type=i.client_type AND g.revoked_at IS NULL AND g.expires_at>clock_timestamp()
   AND NOT EXISTS(SELECT 1 FROM pixels.guest_blocks b WHERE b.guest_id=g.id)
   AND NOT EXISTS(SELECT 1 FROM pixels.guest_source_blocks b WHERE b.source_hash=g.source_hash AND b.expires_at>clock_timestamp())
  ))
 )
) AS "authorized!"
