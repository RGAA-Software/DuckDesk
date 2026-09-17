UPDATE pixels.cache_read_leases l SET expires_at=clock_timestamp()+interval '30 seconds'
FROM pixels.login_sessions s,pixels.users u,pixels.cache_blobs b,pixels.recordings r,pixels.nodes n,pixels.devices d
WHERE l.id=$1 AND l.run_id=$2 AND l.blob_id=$3 AND l.closed_at IS NULL AND l.expires_at>clock_timestamp()
AND s.id=l.login_session_id AND s.user_id=l.origin_user AND u.id=l.origin_user
AND s.authorization_revision=l.owner_revision AND u.authorization_revision=l.owner_revision AND s.client_type=l.client_type
AND s.revoked_at IS NULL AND s.expires_at>clock_timestamp() AND s.absolute_expires_at>clock_timestamp()
AND NOT u.disabled AND u.deleted_at IS NULL AND b.id=l.blob_id AND r.id=b.recording_id AND n.id=r.node_id AND d.id=n.device_id
AND b.state='published' AND b.verified_run=$2 AND EXISTS(SELECT 1 FROM pixels.recording_cache c WHERE c.active_blob_id=b.id)
AND ((l.access_scope='managed' AND u.role IN ('admin','viewer') AND s.client_type='admin_web')
 OR (l.access_scope='device' AND u.role IN ('user','admin') AND NOT d.disabled AND d.deleted_at IS NULL AND
 (EXISTS(SELECT 1 FROM pixels.user_devices ud WHERE ud.device_id=d.id AND ud.user_id=u.id)
 OR EXISTS(SELECT 1 FROM pixels.group_device_grants gd JOIN pixels.user_groups g ON g.id=gd.group_id
 JOIN pixels.group_members gm ON gm.group_id=g.id WHERE gd.device_id=d.id AND gm.user_id=u.id AND g.deleted_at IS NULL))))
RETURNING l.id,l.blob_id,l.run_id,l.expires_at,
GREATEST(0,LEAST(30000,FLOOR(EXTRACT(EPOCH FROM (l.expires_at-clock_timestamp()))*1000)))::bigint AS "remaining_ms!"
