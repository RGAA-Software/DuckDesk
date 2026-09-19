import axiosHttp from "@/http";

export type PlacementRejectionReason =
    | "application_disabled"
    | "application_deleted"
    | "deployment_disabled"
    | "node_disabled"
    | "node_draining"
    | "node_deleted"
    | "device_disabled"
    | "device_deleted"
    | "node_not_ready"
    | "node_disconnected"
    | "node_stale"
    | "control_epoch_mismatch"
    | "public_endpoint_missing"
    | "inventory_missing"
    | "deployment_not_ready"
    | "deployment_generation_mismatch"
    | "deployment_epoch_mismatch"
    | "deployment_endpoint_revision_mismatch"
    | "deployment_revision_mismatch"
    | "capability_missing"
    | "node_capacity_exhausted"
    | "deployment_capacity_exhausted"
    | "port_unavailable"
    | "rdp_workspace_busy"
    | "gpu_inventory_unavailable"
    | "pinned_gpu_missing"
    | "gpu_binding_unavailable"
    | "gpu_metrics_unknown"
    | "gpu_memory_exhausted"
    | "gpu_compute_exhausted"
    | "gpu_encoder_exhausted";

export interface PlacementCandidate {
    rank: number | null;
    deployment_id: string;
    node_id: string;
    gpu_key: string | null;
    eligible: boolean;
    dominant_pressure_per_mille: number | null;
    average_pressure_per_mille: number | null;
    node_slots: number;
    deployment_slots: number;
    gpu_memory_headroom_bytes: number | null;
    gpu_compute_headroom_per_mille: number | null;
    gpu_encoder_headroom_per_mille: number | null;
    rejection_reasons: PlacementRejectionReason[];
}

export interface PlacementPreview {
    application_id: string;
    evaluated_at: string;
    candidates: PlacementCandidate[];
}

export async function previewPlacement(
    applicationId: string,
    deploymentId?: string,
): Promise<PlacementPreview> {
    const response = await axiosHttp.post<PlacementPreview>(
        "/api/console/managed/scheduling/preview",
        {
            application_id: applicationId,
            deployment_id: deploymentId || null,
        },
    );
    return response.data;
}
