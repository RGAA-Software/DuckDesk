import axiosHttp from "@/http";
import type { ManagedNode } from "./managed_node_api";
import type { PlacementPreview } from "./managed_scheduling_api";

export interface InstanceSummary {
    application_id: string;
    node_id: string;
    state: string;
    count: number;
}

export async function getInstanceSummary(): Promise<InstanceSummary[]> {
    const response = await axiosHttp.get<InstanceSummary[]>(
        "/api/console/managed/instance-summary",
    );
    return response.data;
}

// GPU alternatives refer to the same deployment slots. Nodes also share slots and ports.
export function schedulingHeadroom(
    preview: PlacementPreview,
    nodes: ManagedNode[],
    instances: InstanceSummary[],
    isRdp: boolean,
): number {
    const eligibleNodes = new Map<string, { slots: number; deployments: Map<string, number> }>();
    for (const candidate of preview.candidates) {
        if (!candidate.eligible) continue;
        const capacity = eligibleNodes.get(candidate.node_id) ?? {
            slots: candidate.node_slots,
            deployments: new Map<string, number>(),
        };
        capacity.slots = Math.min(capacity.slots, candidate.node_slots);
        capacity.deployments.set(candidate.deployment_id, candidate.deployment_slots);
        eligibleNodes.set(candidate.node_id, capacity);
    }
    let headroom = 0;
    for (const [nodeId, capacity] of eligibleNodes) {
        const node = nodes.find(candidate => candidate.id === nodeId);
        if (node?.application_port_start == null || node.application_port_end == null) continue;
        const occupied = instances
            .filter(instance => instance.node_id === nodeId)
            .reduce((total, instance) => total + instance.count, 0);
        const availablePorts = Math.max(
            0,
            node.application_port_end - node.application_port_start + 1 - occupied,
        );
        const deploymentSlots = [...capacity.deployments.values()].reduce(
            (total, slots) => total + slots,
            0,
        );
        headroom += Math.max(
            0,
            Math.min(capacity.slots, deploymentSlots, availablePorts, isRdp ? 1 : Infinity),
        );
    }
    return headroom;
}
