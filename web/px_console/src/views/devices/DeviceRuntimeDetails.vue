<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import TelemetryTrendChart from "@/views/apps/TelemetryTrendChart.vue";
import { Modal, message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { copyText } from "@/util/clipboard";
import { type ManagedDevice } from "@/model/managed_device_api";
import { managementSnapshotCurrent } from "@/model/management_snapshot";
import { currentNodeTelemetry, nodeOperationalStatus } from "@/model/node_operational_status";
import {
    configureManagedNode,
    createManagedNode,
    deleteManagedNode,
    getManagedNodeTelemetryTrend,
    listManagedNodeTelemetry,
    listManagedNodes,
    type ManagedNode,
    type NodeTelemetry,
    type NodeTelemetryHistory,
    type NodeTelemetryTrend,
    type NodeProduct,
} from "@/model/managed_node_api";

const props = defineProps<{ device: ManagedDevice }>();
const emit = defineEmits<{ changed: [] }>();
const { locale, t } = useI18n();
const nodes = ref<ManagedNode[]>([]);
const deviceNode = computed(() => nodes.value.find(node => node.device_id === props.device.id));
const loading = ref(false);
const saving = ref(false);
const editorOpen = ref(false);
const editing = ref<ManagedNode>();
const form = reactive({
    deviceId: "",
    product: "cloud_node" as NodeProduct,
    maxInstances: 4,
    draining: false,
    disabled: false,
});
const credentialOpen = ref(false);
const nodeToken = ref("");
const telemetryHistoryOpen = ref(false);
const telemetryHistoryLoading = ref(false);
const telemetryHistoryNode = ref<ManagedNode>();
const telemetryHistory = ref<NodeTelemetryHistory[]>([]);
const telemetryTrend = ref<NodeTelemetryTrend>();
const observedAtMs = ref(Date.now());
const monotonicNowMs = ref(performance.now());
const snapshotObservedAtMs = ref<number>();
let statusClockTimer: number | undefined;
const snapshotCurrent = computed(() =>
    managementSnapshotCurrent(snapshotObservedAtMs.value, monotonicNowMs.value),
);

async function refresh() {
    loading.value = true;
    try {
        nodes.value = await listManagedNodes();
        observedAtMs.value = Date.now();
        monotonicNowMs.value = performance.now();
        snapshotObservedAtMs.value = monotonicNowMs.value;
    } catch {
        snapshotObservedAtMs.value = undefined;
        message.error(t("nodes.loadFailed"));
    } finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, {
        deviceId: props.device.id,
        product: "cloud_node",
        maxInstances: 4,
        draining: false,
        disabled: false,
    });
    editorOpen.value = true;
}

function edit(node: ManagedNode) {
    editing.value = node;
    Object.assign(form, {
        deviceId: node.device_id,
        product: node.product,
        maxInstances: node.max_instances,
        draining: node.draining,
        disabled: node.disabled,
    });
    editorOpen.value = true;
}

function showCredential(token: string) {
    nodeToken.value = token;
    credentialOpen.value = true;
}

async function showTelemetryHistory(node: ManagedNode) {
    telemetryHistoryNode.value = node;
    telemetryHistory.value = [];
    telemetryTrend.value = undefined;
    telemetryHistoryOpen.value = true;
    telemetryHistoryLoading.value = true;
    const [history, trend] = await Promise.all([
        listManagedNodeTelemetry(node.id),
        getManagedNodeTelemetryTrend(node.id),
    ]).finally(() => {
        telemetryHistoryLoading.value = false;
    });
    telemetryHistory.value = history;
    telemetryTrend.value = trend;
}

async function save() {
    if (!editing.value && !form.deviceId) {
        message.error(t("nodes.validation.device"));
        return;
    }
    saving.value = true;
    await persistNode().finally(() => {
        saving.value = false;
    });
}

async function persistNode() {
    if (editing.value) {
        await configureManagedNode(editing.value, {
            draining: form.draining,
            disabled: form.disabled,
            max_instances: form.maxInstances,
        });
    }
    if (!editing.value) {
        const result = await createManagedNode(props.device.id, form.product, form.maxInstances);
        showCredential(result.node_token);
    }
    editorOpen.value = false;
    await refresh();
    emit("changed");
}

function remove(node: ManagedNode) {
    Modal.confirm({
        title: t("nodes.confirm.deleteTitle"),
        content: t("nodes.confirm.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteManagedNode(node);
            await refresh();
            emit("changed");
        },
    });
}

async function copyCredential() {
    await copyText(nodeToken.value);
    message.success(t("nodes.messages.copied"));
}

function deviceName(node: ManagedNode) {
    return node.device_id === props.device.id ? props.device.name : node.device_id;
}

function formatPercent(perMille: number | null): string {
    return perMille === null ? t("nodes.unknown") : `${(perMille / 10).toFixed(1)}%`;
}

function formatBytes(bytes: number | null): string {
    if (bytes === null) return t("nodes.unknown");
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        unit += 1;
    }
    return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function formatUsage(total: number | null, available: number | null): string {
    if (total === null || available === null) return t("nodes.unknown");
    return `${formatBytes(Math.max(0, total - available))} / ${formatBytes(total)}`;
}

function formatConsumed(total: number | null, used: number | null): string {
    if (total === null || used === null) return t("nodes.unknown");
    return `${formatBytes(used)} / ${formatBytes(total)}`;
}

function formatProbeState(state: NodeTelemetry["probe_state"] | undefined): string {
    return state ? t(`nodes.telemetryStates.${state}`) : t("nodes.unknown");
}

function formatTimestamp(timestamp: string | null | undefined): string {
    if (!timestamp) return t("nodes.unknown");
    const parsedTimestamp = new Date(timestamp);
    if (Number.isNaN(parsedTimestamp.getTime())) return t("nodes.unknown");
    return new Intl.DateTimeFormat(locale.value, {
        dateStyle: "medium",
        timeStyle: "medium",
    }).format(parsedTimestamp);
}

function telemetryFor(node: ManagedNode): NodeTelemetry | null {
    return currentNodeTelemetry(node, observedAtMs.value, snapshotCurrent.value);
}

function nodeStatusColor(node: ManagedNode): string {
    const status = nodeOperationalStatus(node, snapshotCurrent.value);
    return status === "ready"
        ? "green"
        : status === "offline" || status === "disabled"
          ? "default"
          : "orange";
}

function telemetryHistoryKey(sample: NodeTelemetryHistory): string {
    return `${sample.node_generation}:${sample.report_sequence}`;
}

function formatAge(seconds: number | null | undefined): string {
    if (seconds === null || seconds === undefined) return t("nodes.unknown");
    if (seconds < 60) return t("nodes.ageSeconds", { value: seconds });
    if (seconds < 3600) return t("nodes.ageMinutes", { value: Math.floor(seconds / 60) });
    return t("nodes.ageHours", { value: Math.floor(seconds / 3600) });
}

onMounted(refresh);
onMounted(() => {
    statusClockTimer = window.setInterval(() => {
        observedAtMs.value = Date.now();
        monotonicNowMs.value = performance.now();
    }, 5_000);
});
onBeforeUnmount(() => {
    if (statusClockTimer !== undefined) window.clearInterval(statusClockTimer);
});
useManagementRefresh(["nodes", "instances", "devices"], refresh);
</script>

<template>
    <a-spin :spinning="loading">
        <a-alert
            v-if="!loading && !snapshotCurrent"
            type="warning"
            show-icon
            :message="t('nodes.loadFailed')"
        />
        <template v-if="deviceNode">
            <a-space wrap style="margin-bottom: 16px">
                <a-tag :color="nodeStatusColor(deviceNode)">{{
                    t(
                        `nodes.operationalStates.${nodeOperationalStatus(deviceNode, snapshotCurrent)}`,
                    )
                }}</a-tag>
                <a-button @click="edit(deviceNode)">{{ t("nodes.edit") }}</a-button>
                <a-button @click="showTelemetryHistory(deviceNode)">{{
                    t("nodes.history")
                }}</a-button>

                <a-button danger @click="remove(deviceNode)">{{
                    t("devices.removeRuntime")
                }}</a-button>
            </a-space>
            <a-descriptions bordered size="small" :column="2" style="margin-bottom: 16px">
                <a-descriptions-item :label="t('nodes.product')">{{
                    t(`nodes.products.${deviceNode.product}`)
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('nodes.capacity')">{{
                    deviceNode.max_instances
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('nodes.endpoint')">{{
                    deviceNode.public_host
                        ? `${deviceNode.public_host}:${deviceNode.desktop_port || "-"}`
                        : t("nodes.unknown")
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('nodes.capabilities')"
                    ><a-space
                        ><a-tag v-if="deviceNode.game_hook">Game Hook</a-tag
                        ><a-tag v-if="deviceNode.webview">WebView</a-tag
                        ><a-tag v-if="deviceNode.rdp">RDP</a-tag></a-space
                    ></a-descriptions-item
                >
            </a-descriptions>
            <a-descriptions bordered size="small" :column="3">
                <a-descriptions-item :label="t('nodes.telemetryState')">
                    {{
                        deviceNode.telemetry && !telemetryFor(deviceNode)
                            ? t("nodes.telemetryStale")
                            : formatProbeState(telemetryFor(deviceNode)?.probe_state)
                    }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.sampledAt')">
                    {{ formatTimestamp(deviceNode.telemetry?.sampled_at) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.lastSeen')">
                    {{ formatTimestamp(deviceNode.last_seen) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.version')">
                    {{ deviceNode.product_version_code ?? t("nodes.unknown") }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.cpu')">
                    {{ formatPercent(telemetryFor(deviceNode)?.cpu_utilization_per_mille ?? null) }}
                    /
                    {{ telemetryFor(deviceNode)?.logical_processors ?? t("nodes.unknown") }}
                    {{ t("nodes.logicalProcessors") }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.memory')">
                    {{
                        formatUsage(
                            telemetryFor(deviceNode)?.memory_total_bytes ?? null,
                            telemetryFor(deviceNode)?.memory_available_bytes ?? null,
                        )
                    }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.disk')">
                    {{
                        formatUsage(
                            telemetryFor(deviceNode)?.disk_total_bytes ?? null,
                            telemetryFor(deviceNode)?.disk_free_bytes ?? null,
                        )
                    }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('nodes.gpuInventoryRevision')">
                    {{ telemetryFor(deviceNode)?.gpu_inventory_revision ?? t("nodes.unknown") }}
                </a-descriptions-item>
            </a-descriptions>
            <a-table
                :data-source="deviceNode.gpus"
                row-key="stable_key"
                size="small"
                :pagination="false"
                style="margin-top: 12px"
            >
                <a-table-column :title="t('nodes.gpu')" data-index="name" />
                <a-table-column :title="t('nodes.gpuStableKey')" data-index="stable_key" />
                <a-table-column :title="t('nodes.gpuRuntimeBinding')">
                    <template #default="{ record: gpu }">{{
                        !telemetryFor(deviceNode)
                            ? t("nodes.unknown")
                            : gpu.runtime_binding_ready
                              ? t("nodes.verified")
                              : t("nodes.unverified")
                    }}</template>
                </a-table-column>
                <a-table-column :title="t('nodes.gpuMemory')">
                    <template #default="{ record: gpu }">{{
                        telemetryFor(deviceNode)
                            ? formatConsumed(gpu.dedicated_memory_bytes, gpu.used_memory_bytes)
                            : t("nodes.unknown")
                    }}</template>
                </a-table-column>
                <a-table-column :title="t('nodes.gpuUtilization')">
                    <template #default="{ record: gpu }">{{
                        telemetryFor(deviceNode)
                            ? formatPercent(gpu.utilization_per_mille)
                            : t("nodes.unknown")
                    }}</template>
                </a-table-column>
                <a-table-column :title="t('nodes.encoderUtilization')">
                    <template #default="{ record: gpu }">{{
                        telemetryFor(deviceNode)
                            ? formatPercent(gpu.encoder_utilization_per_mille)
                            : t("nodes.unknown")
                    }}</template>
                </a-table-column>
                <template #emptyText>{{ t("nodes.noGpuInventory") }}</template>
            </a-table>
        </template>
        <a-empty v-else-if="!loading && snapshotCurrent" :description="t('devices.noRuntime')">
            <a-button type="primary" :disabled="device.disabled" @click="create">{{
                t("nodes.create")
            }}</a-button>
        </a-empty>
    </a-spin>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'nodes.edit' : 'nodes.create')"
        :confirm-loading="saving"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('nodes.device')"
                ><a-input :value="device.name" disabled
            /></a-form-item>
            <a-form-item :label="t('nodes.product')"
                ><a-select
                    v-model:value="form.product"
                    :disabled="!!editing"
                    :options="
                        (['cloud_node', 'remote'] as const).map(product => ({
                            value: product,
                            label: t(`nodes.products.${product}`),
                        }))
                    "
            /></a-form-item>
            <a-form-item :label="t('nodes.capacity')"
                ><a-input-number v-model:value="form.maxInstances" :min="1" :max="64"
            /></a-form-item>
            <a-form-item v-if="editing" :label="t('nodes.draining')"
                ><a-switch v-model:checked="form.draining"
            /></a-form-item>
            <a-form-item v-if="editing" :label="t('nodes.disabled')"
                ><a-switch v-model:checked="form.disabled"
            /></a-form-item>
        </a-form>
    </a-modal>

    <a-modal v-model:open="credentialOpen" :title="t('nodes.credentialTitle')" :footer="null">
        <a-alert type="warning" show-icon :message="t('nodes.credentialNotice')" />
        <a-typography-paragraph copyable style="margin-top: 16px; word-break: break-all">{{
            nodeToken
        }}</a-typography-paragraph>
        <a-button type="primary" @click="copyCredential">{{ t("nodes.copyCredential") }}</a-button>
    </a-modal>

    <a-modal
        v-model:open="telemetryHistoryOpen"
        :title="
            t('nodes.historyTitle', {
                node: telemetryHistoryNode ? deviceName(telemetryHistoryNode) : '',
            })
        "
        :footer="null"
        width="1100px"
    >
        <a-alert type="info" show-icon :message="t('nodes.historyNotice')" />
        <a-alert
            v-if="telemetryTrend"
            :type="telemetryTrend.stale ? 'warning' : 'success'"
            show-icon
            :message="
                t(telemetryTrend.stale ? 'nodes.trendStale' : 'nodes.trendFresh', {
                    age: formatAge(telemetryTrend.latest_age_seconds),
                })
            "
            style="margin-top: 12px"
        />
        <TelemetryTrendChart :trend="telemetryTrend" />
        <a-table
            :data-source="telemetryHistory"
            :loading="telemetryHistoryLoading"
            :pagination="false"
            :row-key="telemetryHistoryKey"
            size="small"
            style="margin-top: 12px"
        >
            <a-table-column :title="t('nodes.sampledAt')">
                <template #default="{ record }">{{ formatTimestamp(record.sampled_at) }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.telemetryState')">
                <template #default="{ record }">{{
                    formatProbeState(record.probe_state)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.generation')" data-index="node_generation" />
            <a-table-column :title="t('nodes.sequence')" data-index="report_sequence" />
            <a-table-column :title="t('nodes.cpu')">
                <template #default="{ record }">{{
                    formatPercent(record.cpu_utilization_per_mille)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.memory')">
                <template #default="{ record }">{{
                    formatUsage(record.memory_total_bytes, record.memory_available_bytes)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.disk')">
                <template #default="{ record }">{{
                    formatUsage(record.disk_total_bytes, record.disk_free_bytes)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.gpuCount')">
                <template #default="{ record }">{{ record.gpus.length }}</template>
            </a-table-column>
            <template #emptyText>{{ t("nodes.noHistory") }}</template>
        </a-table>
    </a-modal>
</template>
