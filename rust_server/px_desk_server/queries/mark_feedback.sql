UPDATE pixels.feedback SET processed=$1,revision=revision+1,updated_at=clock_timestamp()
         WHERE id=$2 AND kind=$3 AND revision=$4
         RETURNING id,title,your_name,description,email,wechat,qq,consult_type,version,os,processed,revision,created_at,updated_at
