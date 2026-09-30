SELECT device.id AS device_id, device.public_code, device.name, node.public_host AS host, node.desktop_port AS port
FROM pixels.devices AS device
JOIN pixels.nodes AS node ON node.device_id = device.id
WHERE device.public_code = $1
  AND device.deleted_at IS NULL
  AND NOT device.disabled
  AND node.deleted_at IS NULL
  AND NOT node.disabled
  AND node.state = 'ready'
  AND node.connection_hash IS NOT NULL
  AND node.public_host IS NOT NULL
  AND node.desktop_port IS NOT NULL
