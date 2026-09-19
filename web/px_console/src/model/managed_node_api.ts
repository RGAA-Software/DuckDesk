import axiosHttp from "@/http";

export type NodeProduct = "cloud_node" | "remote";

export interface NodeTelemetry {
    node_id: string;
    node_generation: number;
    report_sequence: number;
    probe_state: "ready" | "partial" | "unavailable";
    sampled_at: string;
    received_at: string;
    logical_processors: number | null;
    cpu_utilization_per_mille: number | null;
    memory_total_bytes: number | null;
    memory_available_bytes: number | null;
    disk_total_bytes: number | null;
    disk_free_bytes: number | null;
    gpu_inventory_revision: number | null;
}

export interface NodeGpuTelemetry {
    node_id: string;
    stable_key: string;
    inventory_revision: number;
    name: string;
    runtime_binding_ready: boolean;
    dedicated_memory_bytes: number | null;
    used_memory_bytes: number | null;
    utilization_per_mille: number | null;
    encoder_utilization_per_mille: number | null;
    sampled_at: string;
    received_at: string;
}

export interface NodeGpuTelemetryHistory extends NodeGpuTelemetry {
    node_generation: number;
    report_sequence: number;
}

export interface NodeTelemetryHistory extends NodeTelemetry {
    gpus: NodeGpuTelemetryHistory[];
}

export interface NodeTelemetryTrendPoint {
    bucket_start: string;
    sample_count: number;
    cpu_known_samples: number;
    cpu_average_per_mille: number | null;
    memory_known_samples: number;
    memory_average_per_mille: number | null;
    disk_known_samples: number;
    disk_average_per_mille: number | null;
    gpu_known_samples: number;
    gpu_average_per_mille: number | null;
    encoder_known_samples: number;
    encoder_average_per_mille: number | null;
}

export interface NodeTelemetryTrend {
    node_id: string;
    evaluated_at: string;
    window_seconds: number;
    bucket_seconds: number;
    latest_received_at: string | null;
    latest_age_seconds: number | null;
    stale: boolean;
    points: NodeTelemetryTrendPoint[];
}

export interface ManagedNode {
    id: string;
    device_id: string;
    product: NodeProduct;
    revision: number;
    generation: number;
    control_epoch: number | null;
    state: string;
    draining: boolean;
    disabled: boolean;
    max_instances: number;
    report_sequence: number;
    last_seen: string | null;
    product_version_code: number | null;
    public_host: string | null;
    desktop_port: number | null;
    application_port_start: number | null;
    application_port_end: number | null;
    game_hook: boolean;
    webview: boolean;
    rdp: boolean;
    endpoint_revision: number;
    fresh: boolean;
    telemetry: NodeTelemetry | null;
    gpus: NodeGpuTelemetry[];
}

interface NodeCredentialResponse {
    node: ManagedNode;
    node_token: string;
}

export async function listManagedNodes(): Promise<ManagedNode[]> {
    const nodes: ManagedNode[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<ManagedNode[]>("/api/console/managed/nodes", {
            params: { after, limit: 100 },
        });
        nodes.push(...response.data);
        if (response.data.length < 100) return nodes;
        after = response.data.at(-1)?.id;
        if (!after) throw new Error("A managed node page did not include its cursor identity");
    }
}

export async function listManagedNodeTelemetry(
    nodeId: string,
    limit = 100,
    before?: NodeTelemetryHistory,
): Promise<NodeTelemetryHistory[]> {
    const response = await axiosHttp.get<NodeTelemetryHistory[]>(
        `/api/console/managed/nodes/${encodeURIComponent(nodeId)}/telemetry`,
        {
            params: {
                limit,
                before_received_at: before?.received_at,
                before_generation: before?.node_generation,
                before_sequence: before?.report_sequence,
            },
        },
    );
    return response.data;
}

export async function getManagedNodeTelemetryTrend(
    nodeId: string,
    windowMinutes = 60,
    bucketSeconds = 60,
): Promise<NodeTelemetryTrend> {
    const response = await axiosHttp.get<NodeTelemetryTrend>(
        `/api/console/managed/nodes/${encodeURIComponent(nodeId)}/telemetry/trend`,
        { params: { window_minutes: windowMinutes, bucket_seconds: bucketSeconds } },
    );
    return response.data;
}

export async function createManagedNode(
    deviceId: string,
    product: NodeProduct,
    maxInstances: number,
): Promise<NodeCredentialResponse> {
    const response = await axiosHttp.post<NodeCredentialResponse>("/api/console/managed/nodes", {
        device_id: deviceId,
        product,
        max_instances: maxInstances,
    });
    return response.data;
}

export async function configureManagedNode(
    node: ManagedNode,
    configuration: { draining: boolean; disabled: boolean; max_instances: number },
): Promise<ManagedNode> {
    const response = await axiosHttp.patch<ManagedNode>(
        `/api/console/managed/nodes/${encodeURIComponent(node.id)}`,
        {
            revision: node.revision,
            configuration,
        },
    );
    return response.data;
}

export async function deleteManagedNode(node: ManagedNode): Promise<void> {
    await axiosHttp.delete(`/api/console/managed/nodes/${encodeURIComponent(node.id)}`, {
        params: { revision: node.revision },
    });
}

export async function rotateManagedNodeCredential(
    node: ManagedNode,
): Promise<NodeCredentialResponse> {
    const response = await axiosHttp.post<NodeCredentialResponse>(
        `/api/console/managed/nodes/${encodeURIComponent(node.id)}/credential`,
        { revision: node.revision },
    );
    return response.data;
}
