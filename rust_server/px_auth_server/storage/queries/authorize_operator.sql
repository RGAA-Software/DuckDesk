SELECT a.id,a.role FROM pixels.authors a JOIN pixels.author_sessions s ON s.author_id=a.id
             WHERE s.token_hash=$1 AND s.revoked_at IS NULL AND s.expires_at>clock_timestamp()
             AND s.authorization_revision=a.authorization_revision FOR SHARE OF a,s
