WITH secret AS (
 UPDATE pixels.workspace_secrets SET key_id=$2,nonce=$3,ciphertext=$4 WHERE workspace_id=$1 RETURNING workspace_id
) UPDATE pixels.rdp_workspaces SET revision=revision+1,updated_at=clock_timestamp()
WHERE id IN (SELECT workspace_id FROM secret) RETURNING id,application_id,deployment_id,node_id,account_name,windows_sid,state,revision,credential_revision,created_at,updated_at
