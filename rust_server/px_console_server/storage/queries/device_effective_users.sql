SELECT user_id AS "user_id!" FROM (
 SELECT user_id FROM pixels.user_devices WHERE device_id=$1
 UNION
 SELECT gm.user_id FROM pixels.group_device_grants gd JOIN pixels.user_groups g ON g.id=gd.group_id
 JOIN pixels.group_members gm ON gm.group_id=g.id WHERE gd.device_id=$1 AND g.deleted_at IS NULL
) users ORDER BY user_id
