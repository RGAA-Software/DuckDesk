SELECT count(*) AS "count!" FROM pixels.users WHERE role='admin' AND NOT disabled AND deleted_at IS NULL
