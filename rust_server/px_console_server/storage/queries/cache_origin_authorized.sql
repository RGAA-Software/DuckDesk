SELECT EXISTS(
 SELECT 1 FROM pixels.cache_blobs b JOIN pixels.login_sessions s ON s.id=b.login_session_id
 JOIN pixels.users u ON u.id=b.origin_user JOIN pixels.recordings r ON r.id=b.recording_id
 JOIN pixels.nodes n ON n.id=r.node_id JOIN pixels.devices d ON d.id=n.device_id
 WHERE b.id=$1 AND s.user_id=u.id AND s.authorization_revision=b.owner_revision AND u.authorization_revision=b.owner_revision
 AND s.client_type=b.client_type AND NOT u.disabled AND u.deleted_at IS NULL
 AND s.revoked_at IS NULL AND s.expires_at>clock_timestamp() AND s.absolute_expires_at>clock_timestamp()
 AND ((b.access_scope='managed' AND s.client_type='admin_web' AND u.role IN ('admin','viewer'))
 OR (b.access_scope='device' AND u.role IN ('user','admin') AND NOT d.disabled AND d.deleted_at IS NULL AND (
 EXISTS(SELECT 1 FROM pixels.user_devices ud WHERE ud.device_id=d.id AND ud.user_id=u.id)
 OR EXISTS(SELECT 1 FROM pixels.group_device_grants gd JOIN pixels.user_groups g ON g.id=gd.group_id
 JOIN pixels.group_members gm ON gm.group_id=g.id WHERE gd.device_id=d.id AND gm.user_id=u.id AND g.deleted_at IS NULL)
 )))) AS "authorized!"
