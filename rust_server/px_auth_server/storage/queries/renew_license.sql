UPDATE pixels.licenses SET revision=$2,mode=$3,not_before=$4,expires_at=$5,max_devices=$6,max_sessions=$7,features=$8,updated_at=clock_timestamp()
                 WHERE id=$1 AND customer_id=$9 AND target_deployment=$10 AND product=$11 AND distribution=$12
                   AND release_namespace=$13 AND oem_id IS NOT DISTINCT FROM $14 AND machine_sha256=$15
