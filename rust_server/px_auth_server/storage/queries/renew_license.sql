UPDATE pixels.licenses SET revision=$2,mode=$3,not_before=$4,expires_at=$5,max_streams=$6,services=$7,updated_at=clock_timestamp()
WHERE id=$1 AND customer_id=$8 AND target_deployment=$9 AND product=$10 AND distribution=$11
                   AND release_namespace=$12 AND oem_id IS NOT DISTINCT FROM $13 AND machine_sha256=$14
