UPDATE pixels.rdp_workspaces SET windows_sid=$2,state='ready',revision=revision+1,updated_at=clock_timestamp()
WHERE id=$1 AND windows_sid IS NULL RETURNING id,application_id,deployment_id,node_id,account_name,windows_sid,state,revision,credential_revision,created_at,updated_at
