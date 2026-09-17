SELECT s.id,s.target_kind,s.device_id,s.application_id,s.instance_id,s.node_id,s.owner_user,s.owner_guest,s.client_type,s.access_role,s.state,s.revision,s.created_at,s.closed_at,
(SELECT min(e.created_at) FROM pixels.resource_session_events e WHERE e.session_id=s.id AND e.kind='connected') AS first_connected_at,
(SELECT count(*) FROM pixels.connection_observations c WHERE c.session_id=s.id) AS "channel_count!"
FROM pixels.resource_sessions s WHERE (s.owner_user=$1 OR s.owner_guest=$2) AND s.client_type=$3 AND ($4::uuid IS NULL OR s.id>$4) ORDER BY s.id LIMIT $5
