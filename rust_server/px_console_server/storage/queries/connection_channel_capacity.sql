SELECT count(*) AS "active!" FROM pixels.connection_observations WHERE session_id=$1 AND state='active'
