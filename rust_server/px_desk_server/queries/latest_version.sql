SELECT id,product,distribution,release_namespace,oem_id,channel,os,architecture,build_number,version,metadata_base_url,targets_base_url,target_name,sha256,platform_signer_sha256,
    size_bytes,created_at FROM pixels.versions
WHERE product=$1 AND distribution=$2 AND release_namespace=$3 AND oem_id IS NOT DISTINCT FROM $4 AND channel=$5 AND os=$6 AND architecture=$7
ORDER BY build_number DESC LIMIT 1
