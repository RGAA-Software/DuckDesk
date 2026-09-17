UPDATE pixels.devices SET deleted_at=clock_timestamp(),revision=revision+1,updated_at=clock_timestamp() WHERE id=$1
RETURNING revision
