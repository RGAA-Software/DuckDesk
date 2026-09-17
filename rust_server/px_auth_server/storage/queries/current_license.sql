SELECT i.license_id,i.revision,i.wire FROM pixels.license_issuances i JOIN pixels.licenses l ON l.id=i.license_id
             WHERE l.id=$1 AND l.revision=$2 AND i.revision=l.revision AND l.revoked_at IS NULL
             AND l.not_before<=clock_timestamp() AND l.expires_at>clock_timestamp()
