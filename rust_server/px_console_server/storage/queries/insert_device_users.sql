INSERT INTO pixels.user_devices(device_id,user_id) SELECT $1,unnest($2::uuid[])
