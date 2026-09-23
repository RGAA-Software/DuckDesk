UPDATE pixels.node_update_tasks SET state='failed',error_code='activation_lease_expired',revision=revision+1,
 updated_at=clock_timestamp(),completed_at=clock_timestamp()
WHERE node_id=$1 AND state='activating' AND lease_until<=clock_timestamp()
