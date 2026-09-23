SELECT id,to_build_number,state,lease_id,revision,error_code
FROM pixels.node_update_tasks WHERE id=$1 AND node_id=$2 FOR UPDATE
