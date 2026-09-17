UPDATE pixels.user_groups SET revision = revision + 1,
    deleted_at = clock_timestamp(), updated_at = clock_timestamp()
WHERE id = $1
