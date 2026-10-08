import { computed, onMounted, onScopeDispose, ref } from "vue";
import { listManagedDevices, type ManagedDevice } from "@/model/managed_device_api";
import { listManagedNodes, type ManagedNode } from "@/model/managed_node_api";
import { listTelemetryAlerts, type TelemetryAlertEvent } from "@/model/telemetry_alert_api";
import { useManagementRefresh } from "@/model/management_events";

export interface DashboardNodeAlert {
    id: string;
    nodeId: string;
    nodeName: string;
    lastSeen: string | null;
    severity: "critical" | "warning";
    event: TelemetryAlertEvent | null;
}

export function useDashboardNodeAlerts() {
    const nodes = ref<ManagedNode[]>([]);
    const devices = ref<ManagedDevice[]>([]);
    const events = ref<TelemetryAlertEvent[]>([]);
    const loading = ref(false);
    const loadFailed = ref(false);
    let disposed = false;
    let refreshTimer: ReturnType<typeof setInterval> | undefined;

    async function collectAlerts(state: "active" | "acknowledged") {
        const collected: TelemetryAlertEvent[] = [];
        const cursors = new Set<string>();
        let before: TelemetryAlertEvent | undefined;
        while (!disposed) {
            const page = await listTelemetryAlerts({ state }, 100, before);
            collected.push(...page);
            if (page.length < 100) return collected;
            before = page.at(-1);
            if (!before) throw new Error("An alert page did not contain its cursor");
            const cursor = `${before.updated_at}:${before.id}`;
            if (cursors.has(cursor)) throw new Error("Alert pagination did not advance");
            cursors.add(cursor);
        }
        return collected;
    }

    const alerts = computed<DashboardNodeAlert[]>(() => {
        const nodeMap = new Map(nodes.value.map(node => [node.id, node]));
        const deviceMap = new Map(devices.value.map(device => [device.id, device]));
        const nodeName = (nodeId: string) => {
            const node = nodeMap.get(nodeId);
            return (node && deviceMap.get(node.device_id)?.name) || node?.public_host || nodeId;
        };
        const offline: DashboardNodeAlert[] = nodes.value
            .filter(node => !node.disabled && !node.fresh)
            .map(node => ({
                id: `offline:${node.id}`,
                nodeId: node.id,
                nodeName: nodeName(node.id),
                lastSeen: node.last_seen,
                severity: "critical",
                event: null,
            }));
        const telemetry: DashboardNodeAlert[] = events.value
            .filter(event => event.state !== "recovered")
            .map(event => ({
                id: event.id,
                nodeId: event.node_id,
                nodeName: nodeName(event.node_id),
                lastSeen: event.last_sampled_at,
                severity: event.severity,
                event,
            }));
        return [...offline, ...telemetry].sort(
            (firstAlert, secondAlert) =>
                Number(secondAlert.severity === "critical") -
                    Number(firstAlert.severity === "critical") ||
                (secondAlert.lastSeen ?? "").localeCompare(firstAlert.lastSeen ?? ""),
        );
    });

    async function refresh() {
        if (disposed || loading.value) return;
        loading.value = true;
        try {
            const [nodeList, deviceList, activeAlerts, acknowledgedAlerts] = await Promise.all([
                listManagedNodes(),
                listManagedDevices(),
                collectAlerts("active"),
                collectAlerts("acknowledged"),
            ]);
            if (disposed) return;
            nodes.value = nodeList;
            devices.value = deviceList;
            // An alert may change state between the two snapshots.
            events.value = [
                ...new Map(
                    [...activeAlerts, ...acknowledgedAlerts].map(event => [event.id, event]),
                ).values(),
            ];
            loadFailed.value = false;
        } catch {
            if (!disposed) loadFailed.value = true;
        } finally {
            if (!disposed) loading.value = false;
        }
    }

    onMounted(() => {
        void refresh();
        refreshTimer = setInterval(() => void refresh(), 30_000);
    });
    useManagementRefresh(["nodes", "devices"], refresh);
    onScopeDispose(() => {
        disposed = true;
        if (refreshTimer !== undefined) clearInterval(refreshTimer);
    });
    return { alerts, loading, loadFailed, refresh };
}
