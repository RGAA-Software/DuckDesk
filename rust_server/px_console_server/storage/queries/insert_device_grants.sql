INSERT INTO pixels.group_device_grants(device_id,group_id) SELECT $1,unnest($2::uuid[])
