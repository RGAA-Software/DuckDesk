SELECT w.id,w.application_id,w.deployment_id,w.node_id,w.account_name,w.windows_sid,w.revision,w.credential_revision,
s.schema_version,s.key_id,s.nonce,s.ciphertext
FROM pixels.instances i
JOIN pixels.rdp_workspaces w ON w.application_id=i.application_id AND w.node_id=i.node_id
 AND w.deployment_id=i.deployment_id AND w.workspace_slot=i.rdp_workspace_slot
JOIN pixels.workspace_secrets s ON s.workspace_id=w.id
WHERE i.id=$1 AND i.kind='rdp'
FOR UPDATE OF w,s
