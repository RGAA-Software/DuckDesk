import axiosHttp from "@/http";

export type DeploymentTarget =
    | { kind: "game_hook"; install_root: string }
    | { kind: "webview" }
    | { kind: "rdp" };

export interface DeploymentConfiguration {
    target: DeploymentTarget;
    gpu_key: string | null;
    capacity: number;
    disabled: boolean;
}

export interface ManagedDeployment {
    id: string;
    application_id: string;
    node_id: string;
    kind: "game_hook" | "webview" | "rdp";
    install_root: string | null;
    gpu_key: string | null;
    capacity: number;
    disabled: boolean;
    revision: number;
    application_revision: number;
    observed_state: string;
    observed_reason: string | null;
    observed_generation: number | null;
    observed_epoch: number | null;
    observed_endpoint_revision: number | null;
    observed_sequence: number;
    observed_at: string | null;
}

export async function listManagedDeployments(): Promise<ManagedDeployment[]> {
    const deployments: ManagedDeployment[] = [];
    let after: string | undefined;
    for (;;) {
        const response = await axiosHttp.get<ManagedDeployment[]>(
            "/api/console/managed/deployments",
            {
                params: { after, limit: 100 },
            },
        );
        deployments.push(...response.data);
        if (response.data.length < 100) return deployments;
        after = response.data.at(-1)?.id;
        if (!after)
            throw new Error("A managed deployment page did not include its cursor identity");
    }
}

export async function createManagedDeployment(
    applicationId: string,
    nodeId: string,
    configuration: DeploymentConfiguration,
): Promise<ManagedDeployment> {
    const response = await axiosHttp.post<ManagedDeployment>("/api/console/managed/deployments", {
        application_id: applicationId,
        node_id: nodeId,
        configuration,
    });
    return response.data;
}

export async function configureManagedDeployment(
    deployment: ManagedDeployment,
    configuration: DeploymentConfiguration,
): Promise<ManagedDeployment> {
    const response = await axiosHttp.patch<ManagedDeployment>(
        `/api/console/managed/deployments/${encodeURIComponent(deployment.id)}`,
        { revision: deployment.revision, configuration },
    );
    return response.data;
}
