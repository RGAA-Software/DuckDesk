SELECT id,release_id,from_build_number,to_build_number,lease_id,lease_until
FROM pixels.node_update_tasks WHERE node_id=$1 AND state='activating'
