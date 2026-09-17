SELECT count(*) AS "count!" FROM pixels.saved_connections WHERE owner_id=$1 AND client_type=$2 AND deleted_at IS NULL
