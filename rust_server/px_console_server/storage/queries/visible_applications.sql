SELECT a.id,a.name,a.kind,a.access_mode,a.revision,a.access_revision FROM pixels.applications a
WHERE NOT a.disabled AND a.deleted_at IS NULL AND ($2::uuid IS NULL OR a.id>$2) AND ($4::uuid IS NULL OR a.id=$4)
AND (a.access_mode='public' OR ($1::uuid IS NOT NULL AND EXISTS(
 SELECT 1 FROM pixels.group_app_grants ga JOIN pixels.user_groups g ON g.id=ga.group_id JOIN pixels.group_members gm ON gm.group_id=g.id
 WHERE ga.application_id=a.id AND gm.user_id=$1 AND g.deleted_at IS NULL)))
ORDER BY a.id LIMIT $3
