INSERT INTO pixels.versions(id,product,distribution,channel,build_number,version,artifact_url,sha256,
    os,architecture,size_bytes,metadata_url,metadata_sha256)
VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13)
RETURNING id,product,distribution,channel,os,architecture,build_number,version,artifact_url,sha256,
    size_bytes,metadata_url,metadata_sha256,created_at
