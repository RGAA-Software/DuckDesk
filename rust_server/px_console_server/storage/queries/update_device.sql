UPDATE pixels.devices SET name=$2,disabled=$3,revision=revision+1,updated_at=clock_timestamp() WHERE id=$1
RETURNING id,public_code,name,platform,disabled,revision,registered_at
