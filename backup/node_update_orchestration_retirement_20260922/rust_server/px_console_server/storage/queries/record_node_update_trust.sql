INSERT INTO pixels.node_update_trust(
    node_id,release_id,node_generation,repository_publication_sha256,trusted_root_version
) VALUES($1,$2,$3,$4,$5)
ON CONFLICT(node_id) DO UPDATE SET
    release_id=EXCLUDED.release_id,
    node_generation=EXCLUDED.node_generation,
    repository_publication_sha256=EXCLUDED.repository_publication_sha256,
    trusted_root_version=EXCLUDED.trusted_root_version,
    revision=pixels.node_update_trust.revision+1,
    observed_at=clock_timestamp()
WHERE pixels.node_update_trust.trusted_root_version<=EXCLUDED.trusted_root_version
RETURNING node_id,release_id,node_generation,repository_publication_sha256,trusted_root_version,revision,observed_at
