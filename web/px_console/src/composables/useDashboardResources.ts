import { computed, onMounted, onScopeDispose, ref } from "vue";
import { listAllAdminUsers } from "@/model/identity_api";
import { listManagedApplications, type ManagedApplication } from "@/model/managed_application_api";
import { listManagedDeployments, type ManagedDeployment } from "@/model/managed_deployment_api";
import { listManagedDevices, type ManagedDevice } from "@/model/managed_device_api";
import { listManagedNodes, type ManagedNode } from "@/model/managed_node_api";
import { listManagedResourceSessions, type ResourceSession } from "@/model/managed_activity_api";
import { previewPlacement, type PlacementPreview } from "@/model/managed_scheduling_api";
import { getInstanceSummary, type InstanceSummary } from "@/model/dashboard_runtime";
import { managementSnapshotCurrent } from "@/model/management_snapshot";
import { useManagementRefresh } from "@/model/management_events";

export function useDashboardResources() {
    const devices = ref<ManagedDevice[]>([]);
    const applications = ref<ManagedApplication[]>([]);
    const deployments = ref<ManagedDeployment[]>([]);
    const nodes = ref<ManagedNode[]>([]);
    const sessions = ref<ResourceSession[]>([]);
    const instances = ref<InstanceSummary[]>([]);
    const previews = ref<Record<string, PlacementPreview | undefined>>({});
    const users = ref(0);
    const loading = ref(false);
    const loadFailed = ref(false);
    const observedAt = ref(Date.now());
    const monotonicNow = ref(performance.now());
    const snapshotAt = ref<number>();
    const current = computed(() => managementSnapshotCurrent(snapshotAt.value, monotonicNow.value));
    let disposed = false;
    let refreshTimer: ReturnType<typeof setInterval> | undefined;
    let clockTimer: ReturnType<typeof setInterval> | undefined;

    async function refresh() {
        if (disposed || loading.value) return;
        loading.value = true;
        try {
            const [
                deviceRows,
                userRows,
                applicationRows,
                deploymentRows,
                nodeRows,
                sessionRows,
                instanceRows,
            ] = await Promise.all([
                listManagedDevices(),
                listAllAdminUsers(),
                listManagedApplications(),
                listManagedDeployments(),
                listManagedNodes(),
                listManagedResourceSessions(),
                getInstanceSummary(),
            ]);
            if (disposed) return;
            devices.value = deviceRows;
            users.value = userRows.length;
            applications.value = applicationRows;
            deployments.value = deploymentRows;
            nodes.value = nodeRows;
            sessions.value = sessionRows;
            instances.value = instanceRows;
            observedAt.value = Date.now();
            monotonicNow.value = performance.now();
            snapshotAt.value = monotonicNow.value;
            loadFailed.value = false;
            previews.value = {};
            // Bound preview work so large application catalogs do not flood the scheduler.
            for (let offset = 0; offset < applicationRows.length && !disposed; offset += 4) {
                const batch = applicationRows.slice(offset, offset + 4);
                const results = await Promise.allSettled(
                    batch.map(application => previewPlacement(application.id)),
                );
                if (disposed) return;
                results.forEach((result, resultIndex) => {
                    const application = batch[resultIndex];
                    if (application && result.status === "fulfilled")
                        previews.value[application.id] = result.value;
                });
            }
        } catch {
            if (!disposed) {
                loadFailed.value = true;
                snapshotAt.value = undefined;
                previews.value = {};
            }
        } finally {
            if (!disposed) loading.value = false;
        }
    }
    onMounted(() => {
        void refresh();
        refreshTimer = setInterval(() => void refresh(), 15_000);
        clockTimer = setInterval(() => {
            observedAt.value = Date.now();
            monotonicNow.value = performance.now();
        }, 1_000);
    });
    useManagementRefresh(
        ["nodes", "devices", "applications", "deployments", "instances", "sessions", "identities"],
        refresh,
    );
    onScopeDispose(() => {
        disposed = true;
        clearInterval(refreshTimer);
        clearInterval(clockTimer);
    });
    return {
        devices,
        users,
        applications,
        deployments,
        nodes,
        sessions,
        instances,
        previews,
        loading,
        loadFailed,
        current,
        observedAt,
        refresh,
    };
}
