INSERT INTO pixels.feedback(id,kind,title,your_name,description,email,wechat,qq,consult_type,version,os,body_sha256)
         VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12) ON CONFLICT(id) DO NOTHING
