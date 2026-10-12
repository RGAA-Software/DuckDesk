import { computed, onScopeDispose, ref } from "vue";
import { isAxiosError } from "axios";
import {
    configureManagedDeployment,
    createManagedDeployment,
    listManagedDeployments,
    type ManagedDeployment,
    type DeploymentTarget,
} from "@/model/managed_deployment_api";

type TargetAction = "create" | "enable" | "disable" | "keep";
type TargetStatus = "pending" | "saving" | "saved" | "unchanged" | "error" | "blocked";
export interface DeploymentTargetResult {
    nodeId: string;
    action: TargetAction;
    status: TargetStatus;
    errorKey?: ReturnType<typeof deploymentErrorKey>;
}
function deploymentErrorKey(error: unknown) {
    const status = isAxiosError(error) ? error.response?.status : undefined;
    if (status === 401) return "deployments.sessionExpired";
    if (status === 403) return "deployments.forbidden";
    if (status === 400) return "deployments.invalidConfiguration";
    if (status === 409) return "deployments.configurationConflict";
    return "deployments.saveFailed";
}
function preservedConfiguration(deployment: ManagedDeployment, disabled: boolean) {
    return {
        target: { kind: deployment.kind },
        gpu_key: deployment.gpu_key,
        capacity: deployment.capacity,
        disabled,
    };
}

export function useDeploymentTargets(
    applicationId: string,
    kind: DeploymentTarget["kind"],
    initialDeployments: ManagedDeployment[],
) {
    const selectedNodeIds = ref(
        initialDeployments
            .filter(
                deployment => deployment.application_id === applicationId && !deployment.disabled,
            )
            .map(deployment => deployment.node_id),
    );
    const newNodeCapacity = ref(1);
    const saving = ref(false);
    const results = ref<DeploymentTargetResult[]>([]);
    const errorKey = ref<ReturnType<typeof deploymentErrorKey>>();
    const complete = ref(false);
    const hasFailures = computed(
        () =>
            !!errorKey.value ||
            results.value.some(result => result.status === "error" || result.status === "blocked"),
    );
    let disposed = false;
    onScopeDispose(() => {
        disposed = true;
    });

    async function save() {
        if (saving.value || disposed) return;
        saving.value = true;
        complete.value = false;
        errorKey.value = undefined;
        results.value = [];
        const selected = new Set(selectedNodeIds.value);
        const capacity = kind === "rdp" ? 1 : newNodeCapacity.value;
        try {
            const deployments = (await listManagedDeployments()).filter(
                deployment => deployment.application_id === applicationId,
            );
            if (disposed) return;
            const currentByNode = new Map(
                deployments.map(deployment => [deployment.node_id, deployment]),
            );
            results.value = [
                ...Array.from(
                    selected,
                    nodeId =>
                        ({
                            nodeId,
                            action: currentByNode.has(nodeId)
                                ? currentByNode.get(nodeId)!.disabled
                                    ? "enable"
                                    : "keep"
                                : "create",
                            status: "pending",
                        }) as DeploymentTargetResult,
                ),
                ...deployments
                    .filter(deployment => !deployment.disabled && !selected.has(deployment.node_id))
                    .map(
                        deployment =>
                            ({
                                nodeId: deployment.node_id,
                                action: "disable",
                                status: "pending",
                            }) as DeploymentTargetResult,
                    ),
            ];
            let additionsFailed = false;
            for (const result of results.value) {
                if (disposed) return;
                if (result.action === "keep") {
                    result.status = "unchanged";
                    continue;
                }
                // Keep the old machine set available if adding a replacement failed.
                if (result.action === "disable" && additionsFailed) {
                    result.status = "blocked";
                    continue;
                }
                result.status = "saving";
                try {
                    if (result.action === "create") {
                        await createManagedDeployment(applicationId, result.nodeId, {
                            target: { kind },
                            gpu_key: null,
                            capacity,
                            disabled: false,
                        });
                    } else {
                        const deployment = currentByNode.get(result.nodeId)!;
                        await configureManagedDeployment(
                            deployment,
                            preservedConfiguration(deployment, result.action === "disable"),
                        );
                    }
                    if (disposed) return;
                    result.status = "saved";
                } catch (error) {
                    if (disposed) return;
                    result.status = "error";
                    result.errorKey = deploymentErrorKey(error);
                    if (result.action !== "disable") additionsFailed = true;
                }
            }
            if (!disposed) complete.value = !hasFailures.value;
        } catch (error) {
            if (!disposed) errorKey.value = deploymentErrorKey(error);
        } finally {
            if (!disposed) saving.value = false;
        }
    }
    return {
        selectedNodeIds,
        newNodeCapacity,
        saving,
        results,
        errorKey,
        complete,
        hasFailures,
        save,
    };
}
