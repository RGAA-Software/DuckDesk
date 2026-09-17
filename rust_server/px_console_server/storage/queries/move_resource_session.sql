UPDATE pixels.resource_sessions SET state=$2,revision=revision+1,
descriptor_hash=NULL,descriptor_expires_at=NULL,
closed_at=CASE WHEN $2='closed' THEN clock_timestamp() ELSE NULL END
WHERE id=$1 AND closed_at IS NULL RETURNING id,target_kind,device_id,application_id,instance_id,node_id,owner_user,owner_guest,login_session_id,owner_revision,client_type,access_role,request_hash,state,revision,node_generation,control_epoch,endpoint_revision,created_at,closed_at
