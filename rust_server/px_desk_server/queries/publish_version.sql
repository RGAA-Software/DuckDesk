INSERT INTO pixels.versions(id,product,distribution,release_namespace,oem_id,channel,build_number,version,metadata_base_url,targets_base_url,target_name,sha256,platform_signer_sha256,
    os,architecture,size_bytes)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16)
RETURNING id,product,distribution,release_namespace,oem_id,channel,os,architecture,build_number,version,metadata_base_url,targets_base_url,target_name,sha256,platform_signer_sha256,
    size_bytes,created_at
