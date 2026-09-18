import axiosHttp from "@/http";

export type NodeProduct = "cloud_node" | "remote";

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
