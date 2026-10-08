SELECT d.id,
       d.application_id,
       d.kind,
       a.executable_path,
       d.gpu_key,
       d.disabled,
       d.revision AS deployment_revision,
       d.application_revision
FROM pixels.application_deployments d
JOIN pixels.applications a ON a.id = d.application_id
WHERE d.node_id = $1
  AND a.deleted_at IS NULL
  AND ($2::uuid IS NULL OR d.id > $2)
ORDER BY d.id
LIMIT $3
