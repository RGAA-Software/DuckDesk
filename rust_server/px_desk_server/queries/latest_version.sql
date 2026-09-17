SELECT id,product,distribution,channel,os,architecture,build_number,version,artifact_url,sha256,
    size_bytes,metadata_url,metadata_sha256,created_at FROM pixels.versions
WHERE product=$1 AND distribution=$2 AND channel=$3 AND os=$4 AND architecture=$5
ORDER BY build_number DESC LIMIT 1
