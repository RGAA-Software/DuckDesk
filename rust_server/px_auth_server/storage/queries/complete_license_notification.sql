UPDATE pixels.license_notification_outbox
SET delivered_at = clock_timestamp(),
    lease_id = NULL,
    lease_until = NULL,
    last_error = NULL
WHERE id = $1
  AND lease_id = $2
  AND delivered_at IS NULL
  AND lease_until > clock_timestamp()
