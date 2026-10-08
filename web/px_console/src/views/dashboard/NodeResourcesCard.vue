<script setup lang="ts">
import { computed, ref } from "vue";
import { useI18n } from "vue-i18n";
import type { ManagedDevice } from "@/model/managed_device_api";
import type { ManagedNode, NodeGpuTelemetry } from "@/model/managed_node_api";
import type { InstanceSummary } from "@/model/dashboard_runtime";
import { currentNodeTelemetry, nodeOperationalStatus } from "@/model/node_operational_status";
import DeviceRuntimeDetails from "@/views/devices/DeviceRuntimeDetails.vue";

const props = defineProps<{
    nodes: ManagedNode[];
    devices: ManagedDevice[];
    instances: InstanceSummary[];
    current: boolean;
    observedAt: number;
}>();
const { t } = useI18n();
const selectedDeviceId = ref<string>();
const selectedDevice = computed(() =>
    props.devices.find(device => device.id === selectedDeviceId.value),
);
const columns = computed(() => [
    { title: t("dashboard.runtime.node"), key: "node", width: 200 },
    { title: t("dashboard.runtime.status"), key: "status", width: 110 },
    { title: "CPU", key: "cpu", width: 150 },
    { title: t("dashboard.runtime.memory"), key: "memory", width: 200 },
    { title: t("dashboard.runtime.gpu"), key: "gpu", width: 260 },
    { title: t("dashboard.runtime.running"), key: "running", width: 110 },
]);
const telemetryFor = (node: ManagedNode) =>
    currentNodeTelemetry(node, props.observedAt, props.current);
function gpuCurrent(node: ManagedNode, gpu: NodeGpuTelemetry): boolean {
    const telemetry = telemetryFor(node);
    const age = props.observedAt - Date.parse(gpu.received_at);
    return (
        !!telemetry &&
        telemetry.gpu_inventory_revision === gpu.inventory_revision &&
        age >= -5_000 &&
        age <= 30_000
    );
}
function percent(value: number | null | undefined): string {
    return value == null ? "—" : `${(value / 10).toFixed(1)}%`;
}
function memory(total: number | null | undefined, used: number | null | undefined): string {
    return total == null || used == null
        ? "—"
        : `${(used / 1073741824).toFixed(1)} / ${(total / 1073741824).toFixed(1)} GiB`;
}
function memoryUsed(node: ManagedNode): number | null {
    const telemetry = telemetryFor(node);
    return telemetry?.memory_total_bytes == null || telemetry.memory_available_bytes == null
        ? null
        : Math.max(0, telemetry.memory_total_bytes - telemetry.memory_available_bytes);
}
function running(nodeId: string): number {
    return props.instances
        .filter(instance => instance.node_id === nodeId && instance.state === "running")
        .reduce((total, instance) => total + instance.count, 0);
}
</script>

<template>
    <a-card :title="t('dashboard.runtime.nodesTitle')" class="node-resources">
        <template #extra
            ><RouterLink to="/devices-list">{{
                t("dashboard.runtime.manageNodes")
            }}</RouterLink></template
        >
        <a-table
            :columns="columns"
            :data-source="nodes"
            row-key="id"
            size="middle"
            :pagination="nodes.length > 6 ? { pageSize: 6 } : false"
            :scroll="{ x: 1030 }"
        >
            <template #bodyCell="{ column, record }">
                <template v-if="column.key === 'node'">
                    <a-button
                        type="link"
                        class="node-name"
                        :disabled="!devices.some(device => device.id === record.device_id)"
                        @click="selectedDeviceId = record.device_id"
                    >
                        {{
                            devices.find(device => device.id === record.device_id)?.name ||
                            record.public_host ||
                            record.id
                        }}
                    </a-button>
                    <div class="secondary">{{ record.public_host || "—" }}</div>
                </template>
                <a-tag
                    v-else-if="column.key === 'status'"
                    :color="
                        nodeOperationalStatus(record, current) === 'ready' ? 'green' : 'default'
                    "
                >
                    {{ t(`nodes.operationalStates.${nodeOperationalStatus(record, current)}`) }}
                </a-tag>
                <template v-else-if="column.key === 'cpu'">
                    {{ percent(telemetryFor(record)?.cpu_utilization_per_mille) }}
                    <a-progress
                        v-if="telemetryFor(record)?.cpu_utilization_per_mille != null"
                        :percent="telemetryFor(record)!.cpu_utilization_per_mille! / 10"
                        :show-info="false"
                        size="small"
                        stroke-color="var(--telemetry-cpu)"
                    />
                </template>
                <template v-else-if="column.key === 'memory'">
                    {{ memory(telemetryFor(record)?.memory_total_bytes, memoryUsed(record)) }}
                    <a-progress
                        v-if="
                            memoryUsed(record) != null && telemetryFor(record)?.memory_total_bytes
                        "
                        :percent="
                            (memoryUsed(record)! / telemetryFor(record)!.memory_total_bytes!) * 100
                        "
                        :show-info="false"
                        size="small"
                        stroke-color="var(--telemetry-memory)"
                    />
                </template>
                <template v-else-if="column.key === 'gpu'">
                    <div v-for="gpu in record.gpus" :key="gpu.stable_key" class="gpu-metric">
                        <div>{{ gpu.name }}</div>
                        <div class="secondary">
                            {{ t("dashboard.runtime.gpuUsage") }}
                            {{
                                gpuCurrent(record, gpu) ? percent(gpu.utilization_per_mille) : "—"
                            }}
                            · {{ t("dashboard.runtime.vram") }}
                            {{
                                gpuCurrent(record, gpu)
                                    ? memory(gpu.dedicated_memory_bytes, gpu.used_memory_bytes)
                                    : "—"
                            }}
                        </div>
                    </div>
                    <span v-if="!record.gpus.length">—</span>
                </template>
                <template v-else-if="column.key === 'running'">{{
                    current && record.fresh
                        ? t("dashboard.runtime.streamCount", { count: running(record.id) })
                        : "—"
                }}</template>
            </template>
        </a-table>
        <div class="secondary metric-note">{{ t("dashboard.runtime.telemetryNotice") }}</div>
        <a-drawer
            :open="!!selectedDevice"
            :title="t('devices.runtimeTitle', { name: selectedDevice?.name || '' })"
            width="min(1100px, 95vw)"
            @close="selectedDeviceId = undefined"
        >
            <DeviceRuntimeDetails
                v-if="selectedDevice"
                :key="selectedDevice.id"
                :device="selectedDevice"
            />
        </a-drawer>
    </a-card>
</template>

<style scoped>
.node-name {
    padding: 0;
    height: auto;
    white-space: normal;
    text-align: start;
}
.secondary {
    opacity: 0.65;
    font-size: 12px;
    overflow-wrap: anywhere;
}
.metric-note {
    margin-top: 12px;
}
.gpu-metric + .gpu-metric {
    margin-top: 8px;
}
</style>
