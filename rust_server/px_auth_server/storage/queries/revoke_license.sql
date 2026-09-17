UPDATE pixels.licenses SET revision=revision+1,revoked_at=clock_timestamp(),updated_at=clock_timestamp()
             WHERE id=$1 AND revision=$2 AND revoked_at IS NULL RETURNING revision
