UPDATE pixels.node_telemetry_alert_policies
SET revision=revision+1,cpu_warning_per_mille=$3,cpu_critical_per_mille=$4,
 memory_warning_per_mille=$5,memory_critical_per_mille=$6,disk_warning_per_mille=$7,disk_critical_per_mille=$8,
 gpu_warning_per_mille=$9,gpu_critical_per_mille=$10,trigger_samples=$11,recovery_samples=$12,
 recovery_hysteresis_per_mille=$13,updated_at=clock_timestamp()
WHERE node_id=$1 AND revision=$2
RETURNING node_id,revision,cpu_warning_per_mille,cpu_critical_per_mille,memory_warning_per_mille,memory_critical_per_mille,
 disk_warning_per_mille,disk_critical_per_mille,gpu_warning_per_mille,gpu_critical_per_mille,trigger_samples,recovery_samples,
 recovery_hysteresis_per_mille,updated_at
