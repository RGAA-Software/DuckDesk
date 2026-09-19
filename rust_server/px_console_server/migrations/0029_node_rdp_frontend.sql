ALTER TABLE pixels.nodes
ADD COLUMN rdp_domain TEXT,
ADD COLUMN rdp_proxy_certificate_sha256 TEXT,
ADD CONSTRAINT nodes_rdp_frontend_identity CHECK (
    (rdp AND rdp_domain IS NOT NULL AND rdp_domain ~ '^[A-Za-z0-9-]{1,15}$'
        AND rdp_proxy_certificate_sha256 ~ '^[0-9a-f]{64}$')
    OR (NOT rdp AND rdp_domain IS NULL AND rdp_proxy_certificate_sha256 IS NULL)
);

GRANT UPDATE(rdp_domain,rdp_proxy_certificate_sha256) ON pixels.nodes TO pixels_console_runtime;
