INSERT INTO pixels.resource_sessions
(id,target_kind,device_id,application_id,instance_id,node_id,owner_user,owner_guest,login_session_id,owner_revision,client_type,access_role,request_id,request_hash,node_generation,control_epoch,endpoint_revision)
VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17)
RETURNING id,target_kind,device_id,application_id,instance_id,node_id,owner_user,owner_guest,login_session_id,owner_revision,client_type,access_role,request_hash,state,revision,node_generation,control_epoch,endpoint_revision,created_at,closed_at
