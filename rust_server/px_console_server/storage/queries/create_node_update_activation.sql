INSERT INTO pixels.node_update_tasks(id,node_id,release_id,from_build_number,to_build_number,state,lease_id,lease_until)
VALUES($1,$2,$3,$4,$5,'activating',$6,clock_timestamp()+interval '10 minutes')
RETURNING id,lease_id,lease_until
