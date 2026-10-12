INSERT INTO pixels.rdp_workspaces(id,application_id,deployment_id,node_id,account_name,workspace_slot)
SELECT $1,$2,$3,$4,$5,rdp_workspace_slot FROM pixels.instances
WHERE id=$6 AND application_id=$2 AND deployment_id=$3 AND node_id=$4 AND kind='rdp'
