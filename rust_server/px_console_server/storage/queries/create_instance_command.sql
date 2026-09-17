INSERT INTO pixels.instance_commands(id,instance_id,node_id,node_generation,control_epoch,instance_revision,kind,deadline)
VALUES($1,$2,$3,$4,$5,$6,$7,clock_timestamp()+interval '60 seconds')
