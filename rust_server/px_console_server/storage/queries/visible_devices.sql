SELECT d.id,d.public_code,d.name,d.platform,d.disabled,d.revision,d.registered_at FROM pixels.devices d
WHERE NOT d.disabled AND d.deleted_at IS NULL AND ($2::uuid IS NULL OR d.id>$2) AND ($4::uuid IS NULL OR d.id=$4)
AND (EXISTS(SELECT 1 FROM pixels.user_devices ud WHERE ud.device_id=d.id AND ud.user_id=$1)
 OR EXISTS(SELECT 1 FROM pixels.group_device_grants gd JOIN pixels.user_groups g ON g.id=gd.group_id
 JOIN pixels.group_members gm ON gm.group_id=g.id
 WHERE gd.device_id=d.id AND g.deleted_at IS NULL AND gm.user_id=$1))
ORDER BY d.id LIMIT $3
