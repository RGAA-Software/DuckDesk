UPDATE pixels.applications SET name=$2,access_mode=$3,entry_url=$4,executable_relative=$5,arguments=$6,bitrate_kbps=$7,codec=$8,
allow_observer=$9,allow_takeover=$10,disabled=$11,revision=revision+1,access_revision=access_revision+$12,updated_at=clock_timestamp() WHERE id=$1
RETURNING id,name,kind,access_mode,entry_url,executable_relative,arguments,bitrate_kbps,codec,allow_observer,allow_takeover,disabled,revision,access_revision
