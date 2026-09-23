UPDATE pixels.licenses SET revision=$2,expires_at=$3,max_streams=$4,services=$5,updated_at=clock_timestamp()
WHERE id=$1 AND customer_id=$6 AND target_deployment=$7
