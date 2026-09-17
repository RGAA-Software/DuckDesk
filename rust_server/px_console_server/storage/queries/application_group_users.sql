SELECT DISTINCT gm.user_id FROM pixels.group_app_grants ga
JOIN pixels.user_groups g ON g.id=ga.group_id JOIN pixels.group_members gm ON gm.group_id=g.id
WHERE ga.application_id=$1 AND g.deleted_at IS NULL ORDER BY gm.user_id
