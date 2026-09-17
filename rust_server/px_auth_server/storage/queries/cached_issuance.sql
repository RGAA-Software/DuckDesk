SELECT i.license_id,i.revision,i.wire FROM pixels.license_issuances i JOIN pixels.licenses l ON l.id=i.license_id
                 WHERE i.id=$1 AND i.revision=l.revision AND l.revoked_at IS NULL AND l.expires_at>clock_timestamp() FOR SHARE OF l
