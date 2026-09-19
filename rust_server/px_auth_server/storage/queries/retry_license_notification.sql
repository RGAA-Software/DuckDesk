UPDATE pixels.license_notification_outbox
SET available_at = clock_timestamp() + make_interval(secs => $3),
    lease_id = NULL,
    lease_until = NULL,
    last_error = $4
WHERE id = $1
  AND lease_id = $2
  AND delivered_at IS NULL
  AND lease_until > clock_timestamp()
