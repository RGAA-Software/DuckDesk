SELECT id,target_kind,device_id,application_id,instance_id,node_id,owner_user,owner_guest,login_session_id,owner_revision,client_type,access_role,request_hash,state,revision,node_generation,control_epoch,endpoint_revision,created_at,closed_at
FROM pixels.resource_sessions
WHERE instance_id=$1 AND closed_at IS NULL
ORDER BY id
FOR UPDATE
