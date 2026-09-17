SELECT EXISTS(SELECT 1 FROM pixels.instance_commands WHERE instance_id=$1 AND attempts>0) AS "dispatched!"
