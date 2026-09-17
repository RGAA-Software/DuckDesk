INSERT INTO pixels.group_app_grants(application_id,group_id) SELECT $1,unnest($2::uuid[])
