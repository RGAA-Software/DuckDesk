SELECT w.id,w.application_id,w.deployment_id,w.node_id,w.account_name,w.windows_sid,w.revision,w.credential_revision,
s.schema_version,s.key_id,s.nonce,s.ciphertext
FROM pixels.resource_sessions r
JOIN pixels.instances i ON i.id=r.instance_id AND i.application_id=r.application_id AND i.node_id=r.node_id
JOIN pixels.nodes n ON n.id=r.node_id
JOIN pixels.rdp_workspaces w ON w.application_id=i.application_id AND w.deployment_id=i.deployment_id AND w.node_id=i.node_id
JOIN pixels.workspace_secrets s ON s.workspace_id=w.id
WHERE r.id=$1 AND r.target_kind='cloud_application' AND r.client_type='panel' AND r.access_role='controller'
AND r.state IN ('pending','connected') AND r.descriptor_hash IS NOT NULL AND r.descriptor_expires_at>clock_timestamp()
AND i.kind='rdp' AND i.state='running' AND i.desired_state='running' AND i.ended_at IS NULL
AND r.node_generation=n.generation AND r.control_epoch=n.control_epoch AND r.endpoint_revision=n.endpoint_revision
AND n.state='ready' AND NOT n.disabled AND n.deleted_at IS NULL
AND n.control_epoch=(SELECT epoch FROM pixels.control_runtime)
AND n.last_seen>clock_timestamp()-interval '30 seconds'
AND w.state='ready'
AND ((r.owner_user IS NOT NULL AND EXISTS(
    SELECT 1 FROM pixels.login_sessions l JOIN pixels.users u ON u.id=l.user_id
    WHERE l.id=r.login_session_id AND u.id=r.owner_user AND u.role IN ('user','admin')
    AND NOT u.disabled AND u.deleted_at IS NULL AND u.authorization_revision=r.owner_revision
    AND l.authorization_revision=r.owner_revision AND l.client_type=r.client_type
    AND l.revoked_at IS NULL AND l.expires_at>clock_timestamp() AND l.absolute_expires_at>clock_timestamp()
)) OR (r.owner_guest IS NOT NULL AND EXISTS(
    SELECT 1 FROM pixels.guest_sessions g WHERE g.id=r.owner_guest AND g.revision=r.owner_revision
    AND g.client_type=r.client_type AND g.revoked_at IS NULL AND g.expires_at>clock_timestamp()
    AND NOT EXISTS(SELECT 1 FROM pixels.guest_blocks b WHERE b.guest_id=g.id)
    AND NOT EXISTS(SELECT 1 FROM pixels.guest_source_blocks b WHERE b.source_hash=g.source_hash AND b.expires_at>clock_timestamp())
)))
FOR SHARE OF r,i,w,s
