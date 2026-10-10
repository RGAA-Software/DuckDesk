SELECT EXISTS(SELECT 1 FROM pixels.resource_sessions WHERE id=$1 AND state='connected'
AND access_role IN ('controller','file_transfer') AND descriptor_expires_at>clock_timestamp()) AS "ready!"
