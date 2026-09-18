SELECT EXISTS(
 SELECT 1 FROM pixels.resource_sessions s WHERE s.id=$1 AND s.owner_user=$2
) AS "owned!"
