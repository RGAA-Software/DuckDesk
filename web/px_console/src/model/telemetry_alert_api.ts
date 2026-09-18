import axiosHttp from "@/http";

export type TelemetryAlertMetric = "cpu" | "memory" | "disk" | "gpu";
export type TelemetryAlertSeverity = "warning" | "critical";
export type TelemetryAlertState = "active" | "acknowledged" | "recovered";

export interface TelemetryAlertEvent {
    id: string;
    node_id: string;
    metric: TelemetryAlertMetric;
    resource_key: string;
    resource_name: string;
    severity: TelemetryAlertSeverity;
    state: TelemetryAlertState;
    threshold_per_mille: number;
    first_value_per_mille: number;
    latest_value_per_mille: number;
    peak_value_per_mille: number;
    occurrence_count: number;
    first_sampled_at: string;
    last_sampled_at: string;
    created_at: string;
    updated_at: string;
    acknowledged_by: string | null;
    acknowledged_at: string | null;
    recovered_at: string | null;
    revision: number;
}

export interface TelemetryAlertFilters {
    nodeId?: string;
    metric?: TelemetryAlertMetric;
    severity?: TelemetryAlertSeverity;
    state?: TelemetryAlertState;
}

export interface TelemetryAlertPolicy {
    node_id: string;
    revision: number;
    cpu_warning_per_mille: number;
    cpu_critical_per_mille: number;
    memory_warning_per_mille: number;
    memory_critical_per_mille: number;
    disk_warning_per_mille: number;
    disk_critical_per_mille: number;
    gpu_warning_per_mille: number;
    gpu_critical_per_mille: number;
    trigger_samples: number;
    recovery_samples: number;
    recovery_hysteresis_per_mille: number;
    updated_at: string;
}

export async function listTelemetryAlerts(
    filters: TelemetryAlertFilters = {},
    limit = 100,
    before?: TelemetryAlertEvent,
): Promise<TelemetryAlertEvent[]> {
    const response = await axiosHttp.get<TelemetryAlertEvent[]>(
        "/api/console/managed/telemetry-alerts",
        {
            params: {
                node_id: filters.nodeId,
                metric: filters.metric,
                severity: filters.severity,
                state: filters.state,
                before_updated_at: before?.updated_at,
                before_id: before?.id,
                limit,
            },
        },
    );
    return response.data;
}

export async function getTelemetryAlert(eventId: string): Promise<TelemetryAlertEvent> {
    const response = await axiosHttp.get<TelemetryAlertEvent>(
        `/api/console/managed/telemetry-alerts/${encodeURIComponent(eventId)}`,
    );
    return response.data;
}

export async function acknowledgeTelemetryAlert(
    event: TelemetryAlertEvent,
): Promise<TelemetryAlertEvent> {
    const response = await axiosHttp.patch<TelemetryAlertEvent>(
        `/api/console/managed/telemetry-alerts/${encodeURIComponent(event.id)}/acknowledgement`,
        { revision: event.revision },
    );
    return response.data;
}

export async function getTelemetryAlertPolicy(nodeId: string): Promise<TelemetryAlertPolicy> {
    const response = await axiosHttp.get<TelemetryAlertPolicy>(
        `/api/console/managed/nodes/${encodeURIComponent(nodeId)}/telemetry-alert-policy`,
    );
    return response.data;
}

export async function configureTelemetryAlertPolicy(
    policy: TelemetryAlertPolicy,
): Promise<TelemetryAlertPolicy> {
    const response = await axiosHttp.patch<TelemetryAlertPolicy>(
        `/api/console/managed/nodes/${encodeURIComponent(policy.node_id)}/telemetry-alert-policy`,
        {
            revision: policy.revision,
            cpu_warning_per_mille: policy.cpu_warning_per_mille,
            cpu_critical_per_mille: policy.cpu_critical_per_mille,
            memory_warning_per_mille: policy.memory_warning_per_mille,
            memory_critical_per_mille: policy.memory_critical_per_mille,
            disk_warning_per_mille: policy.disk_warning_per_mille,
            disk_critical_per_mille: policy.disk_critical_per_mille,
            gpu_warning_per_mille: policy.gpu_warning_per_mille,
            gpu_critical_per_mille: policy.gpu_critical_per_mille,
            trigger_samples: policy.trigger_samples,
            recovery_samples: policy.recovery_samples,
            recovery_hysteresis_per_mille: policy.recovery_hysteresis_per_mille,
        },
    );
    return response.data;
}
