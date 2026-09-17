-- Required fresh-baseline dimensions. No defaults or backfill for retired development data.
ALTER TABLE pixels.versions
    ADD COLUMN os TEXT NOT NULL CHECK (os IN ('windows','linux','android')),
    ADD COLUMN architecture TEXT NOT NULL CHECK (architecture IN ('x86_64','aarch64')),
    ADD COLUMN size_bytes BIGINT NOT NULL CHECK (size_bytes > 0 AND size_bytes <= 1099511627776),
    ADD COLUMN metadata_url TEXT NOT NULL CHECK (char_length(metadata_url) >= 9 AND char_length(metadata_url) <= 2048
        AND metadata_url ~ '^https://[^/@[:space:]]+([/:]|$)' AND metadata_url !~ '[?#[:space:]]'),
    ADD COLUMN metadata_sha256 TEXT NOT NULL CHECK (metadata_sha256 ~ '^[a-f0-9]{64}$'),
    ADD CONSTRAINT versions_platform CHECK (
        (product='android' AND os='android' AND architecture='aarch64') OR
        (product IN ('cloud_node','client','remote') AND os='windows' AND architecture='x86_64') OR
        (product='server' AND os IN ('windows','linux') AND architecture='x86_64')),
    ADD CONSTRAINT versions_stable_url CHECK (artifact_url !~ '[?#[:space:]]');
ALTER TABLE pixels.versions DROP CONSTRAINT versions_product_distribution_channel_build_number_key;
ALTER TABLE pixels.versions ADD UNIQUE(product,distribution,channel,os,architecture,build_number);
