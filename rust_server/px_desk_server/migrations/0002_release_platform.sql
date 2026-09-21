-- Required fresh-baseline dimensions. No defaults or backfill for retired development data.
ALTER TABLE pixels.versions
    ADD COLUMN os TEXT NOT NULL CHECK (os IN ('windows','linux','android')),
    ADD COLUMN architecture TEXT NOT NULL CHECK (architecture IN ('x86_64','aarch64')),
    ADD COLUMN size_bytes BIGINT NOT NULL CHECK (size_bytes > 0 AND size_bytes <= 1099511627776),
    ADD CONSTRAINT versions_platform CHECK (
        (product='android' AND os='android' AND architecture='aarch64') OR
        (product IN ('cloud_node','client','remote') AND os='windows' AND architecture='x86_64') OR
        (product='server' AND os IN ('windows','linux') AND architecture='x86_64')),
    ADD CONSTRAINT versions_platform_signer CHECK (
        (os IN ('windows','android') AND platform_signer_sha256 ~ '^[a-f0-9]{64}$')
        OR (os='linux' AND platform_signer_sha256 IS NULL));
ALTER TABLE pixels.versions DROP CONSTRAINT versions_release_identity_build;
ALTER TABLE pixels.versions ADD CONSTRAINT versions_release_identity_platform_build
    UNIQUE(product,distribution,release_namespace,channel,os,architecture,build_number);
