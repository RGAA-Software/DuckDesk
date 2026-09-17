SELECT id,title,your_name,description,email,wechat,qq,consult_type,version,os,processed,revision,created_at,updated_at
         FROM pixels.feedback WHERE kind=$1 AND ($2::boolean IS NULL OR processed=$2)
         ORDER BY created_at DESC,id DESC LIMIT $3 OFFSET $4
