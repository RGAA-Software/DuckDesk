INSERT INTO pixels.group_members(group_id, user_id) SELECT $1, unnest($2::uuid[])
