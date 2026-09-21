WITH pending AS (
    SELECT candidate.id
    FROM pixels.license_notification_outbox candidate
    WHERE candidate.delivered_at IS NULL
      AND candidate.available_at <= clock_timestamp()
      AND (candidate.lease_until IS NULL OR candidate.lease_until <= clock_timestamp())
      AND NOT EXISTS (
          SELECT 1
          FROM pixels.license_notification_outbox predecessor
          WHERE predecessor.license_id = candidate.license_id
            AND predecessor.revision < candidate.revision
            AND predecessor.delivered_at IS NULL
      )
    ORDER BY candidate.created_at, candidate.id
    FOR UPDATE SKIP LOCKED
    LIMIT $1
), claimed AS (
    UPDATE pixels.license_notification_outbox notification
    SET lease_id = $2,
        lease_until = clock_timestamp() + interval '30 seconds',
        attempts = attempts + 1
    FROM pending
    WHERE notification.id = pending.id
    RETURNING notification.id,
              notification.license_id,
              notification.revision,
              notification.action,
              notification.issuance_id,
              notification.lease_id,
              notification.attempts
)
SELECT claimed.id,
       claimed.license_id,
       claimed.revision,
       claimed.action,
       license.target_deployment AS deployment_id,
       license.product,
       license.distribution,
       license.release_namespace,
       license.oem_id,
       license.machine_sha256,
       issuance.wire AS "wire?",
       claimed.lease_id AS "lease_id!",
       claimed.attempts
FROM claimed
JOIN pixels.licenses license ON license.id = claimed.license_id
LEFT JOIN pixels.license_issuances issuance ON issuance.id = claimed.issuance_id
ORDER BY claimed.revision, claimed.id
