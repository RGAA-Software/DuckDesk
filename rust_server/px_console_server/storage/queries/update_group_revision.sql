UPDATE pixels.user_groups SET revision = revision + 1, updated_at = clock_timestamp()
WHERE id = $1 RETURNING id, name, remark, revision
