INSERT INTO pixels.customers(id,name,name_normalized,remark) VALUES($1,$2,$3,$4) RETURNING id,name,remark
