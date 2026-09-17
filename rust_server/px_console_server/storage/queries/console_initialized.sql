SELECT EXISTS(SELECT 1 FROM pixels.users WHERE role='admin' AND NOT disabled AND deleted_at IS NULL) AS "initialized!"
