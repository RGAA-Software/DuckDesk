<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import TelemetryTrendChart from "@/views/apps/TelemetryTrendChart.vue";
import { Modal, message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { copyText } from "@/util/clipboard";
import { listManagedDevices, type ManagedDevice } from "@/model/managed_device_api";
import { managementSnapshotCurrent } from "@/model/management_snapshot";
import { currentNodeTelemetry, nodeOperationalStatus } from "@/model/node_operational_status";
import {
    configureManagedNode,
    createManagedNode,
    deleteManagedNode,
    getManagedNodeTelemetryTrend,
    listManagedNodeTelemetry,
    listManagedNodes,
    rotateManagedNodeCredential,
    type ManagedNode,
    type NodeTelemetry,
    type NodeTelemetryHistory,
    type NodeTelemetryTrend,
    type NodeProduct,
} from "@/model/managed_node_api";

const { locale, t } = useI18n();
const nodes = ref<ManagedNode[]>([]);
const devices = ref<ManagedDevice[]>([]);
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
const snapshotCurrent = computed(
    () => managementSnapshotCurrent(snapshotObservedAtMs.value, monotonicNowMs.value),
);

const availableDevices = computed(() => {
    const assigned = new Set(nodes.value.map(node => node.device_id));
    return devices.value.filter(
        device =>
            !device.disabled &&
            (!assigned.has(device.id) || device.id === editing.value?.device_id),
    );
});

async function refresh() {
    loading.value = true;
    try {
        const [managedNodes, managedDevices] = await Promise.all([
            listManagedNodes(),
            listManagedDevices(),
        ]);
        nodes.value = managedNodes;
        devices.value = managedDevices;
        observedAtMs.value = Date.now();
        monotonicNowMs.value = performance.now();
        snapshotObservedAtMs.value = monotonicNowMs.value;
    }
    catch {
        snapshotObservedAtMs.value = undefined;
        message.error(t("nodes.loadFailed"));
    }
    finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, {
        deviceId: availableDevices.value[0]?.id || "",
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
        const result = await createManagedNode(form.deviceId, form.product, form.maxInstances);
        showCredential(result.node_token);
    }
    editorOpen.value = false;
    await refresh();
}

function rotateCredential(node: ManagedNode) {
    Modal.confirm({
        title: t("nodes.confirm.rotateTitle"),
        content: t("nodes.confirm.rotateImpact"),
        okType: "danger",
        async onOk() {
            const result = await rotateManagedNodeCredential(node);
            showCredential(result.node_token);
            await refresh();
        },
    });
}

function remove(node: ManagedNode) {
    Modal.confirm({
        title: t("nodes.confirm.deleteTitle"),
        content: t("nodes.confirm.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteManagedNode(node);
            await refresh();
        },
    });
}

async function copyCredential() {
    await copyText(nodeToken.value);
    message.success(t("nodes.messages.copied"));
}

function deviceName(node: ManagedNode) {
    return devices.value.find(device => device.id === node.device_id)?.name || node.device_id;
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
    return status === "ready" ? "green" : status === "offline" || status === "disabled" ? "default" : "orange";
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
useManagementRefresh(["nodes", "instances"], refresh);
</script>

<template>
    <a-card :title="t('nodes.title')">
        <template #extra
            ><a-button type="primary" :disabled="availableDevices.length === 0" @click="create">{{
                t("nodes.create")
            }}</a-button></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('nodes.statusNotice')"
            style="margin-bottom: 12px"
        />
        <a-table :data-source="nodes" row-key="id" :loading="loading" :pagination="false">
            <template #expandedRowRender="{ record }">
                <a-descriptions bordered size="small" :column="3">
                    <a-descriptions-item :label="t('nodes.telemetryState')">
                        {{
                            record.telemetry && !telemetryFor(record)
                                ? t("nodes.telemetryStale")
                                : formatProbeState(telemetryFor(record)?.probe_state)
                        }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.sampledAt')">
                        {{ formatTimestamp(record.telemetry?.sampled_at) }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.lastSeen')">
                        {{ formatTimestamp(record.last_seen) }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.version')">
                        {{ record.product_version_code ?? t("nodes.unknown") }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.cpu')">
                        {{ formatPercent(telemetryFor(record)?.cpu_utilization_per_mille ?? null) }} /
                        {{ telemetryFor(record)?.logical_processors ?? t("nodes.unknown") }}
                        {{ t("nodes.logicalProcessors") }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.memory')">
                        {{
                            formatUsage(
                                telemetryFor(record)?.memory_total_bytes ?? null,
                                telemetryFor(record)?.memory_available_bytes ?? null,
                            )
                        }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.disk')">
                        {{
                            formatUsage(
                                telemetryFor(record)?.disk_total_bytes ?? null,
                                telemetryFor(record)?.disk_free_bytes ?? null,
                            )
                        }}
                    </a-descriptions-item>
                    <a-descriptions-item :label="t('nodes.gpuInventoryRevision')">
                        {{ telemetryFor(record)?.gpu_inventory_revision ?? t("nodes.unknown") }}
                    </a-descriptions-item>
                </a-descriptions>
                <a-table
                    :data-source="record.gpus"
                    row-key="stable_key"
                    size="small"
                    :pagination="false"
                    style="margin-top: 12px"
                >
                    <a-table-column :title="t('nodes.gpu')" data-index="name" />
                    <a-table-column :title="t('nodes.gpuStableKey')" data-index="stable_key" />
                    <a-table-column :title="t('nodes.gpuRuntimeBinding')">
                        <template #default="{ record: gpu }">{{
                            !telemetryFor(record)
                                ? t("nodes.unknown")
                                : gpu.runtime_binding_ready
                                  ? t("nodes.verified")
                                  : t("nodes.unverified")
                        }}</template>
                    </a-table-column>
                    <a-table-column :title="t('nodes.gpuMemory')">
                        <template #default="{ record: gpu }">{{
                            telemetryFor(record)
                                ? formatConsumed(gpu.dedicated_memory_bytes, gpu.used_memory_bytes)
                                : t("nodes.unknown")
                        }}</template>
                    </a-table-column>
                    <a-table-column :title="t('nodes.gpuUtilization')">
                        <template #default="{ record: gpu }">{{
                            telemetryFor(record)
                                ? formatPercent(gpu.utilization_per_mille)
                                : t("nodes.unknown")
                        }}</template>
                    </a-table-column>
                    <a-table-column :title="t('nodes.encoderUtilization')">
                        <template #default="{ record: gpu }">{{
                            telemetryFor(record)
                                ? formatPercent(gpu.encoder_utilization_per_mille)
                                : t("nodes.unknown")
                        }}</template>
                    </a-table-column>
                    <template #emptyText>{{ t("nodes.noGpuInventory") }}</template>
                </a-table>
            </template>
            <a-table-column :title="t('nodes.device')"
                ><template #default="{ record }">{{ deviceName(record) }}</template></a-table-column
            >
            <a-table-column :title="t('nodes.product')"
                ><template #default="{ record }">{{
                    t(`nodes.products.${record.product}`)
                }}</template></a-table-column
            >
            <a-table-column :title="t('nodes.state')"
                ><template #default="{ record }"
                    ><a-tag :color="nodeStatusColor(record)"
                        >{{ t(`nodes.operationalStates.${nodeOperationalStatus(record, snapshotCurrent)}`) }}</a-tag
                    ></template
                ></a-table-column
            >
            <a-table-column :title="t('nodes.capacity')" data-index="max_instances" />
            <a-table-column :title="t('nodes.cpu')">
                <template #default="{ record }">{{
                    formatPercent(telemetryFor(record)?.cpu_utilization_per_mille ?? null)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.memory')">
                <template #default="{ record }">{{
                    formatUsage(
                        telemetryFor(record)?.memory_total_bytes ?? null,
                        telemetryFor(record)?.memory_available_bytes ?? null,
                    )
                }}</template>
            </a-table-column>
            <a-table-column :title="t('nodes.endpoint')"
                ><template #default="{ record }">{{
                    record.public_host ? `${record.public_host}:${record.desktop_port || "-"}` : "-"
                }}</template></a-table-column
            >
            <a-table-column :title="t('nodes.capabilities')"
                ><template #default="{ record }"
                    ><a-space
                        ><a-tag v-if="record.game_hook">Game Hook</a-tag
                        ><a-tag v-if="record.webview">WebView</a-tag
                        ><a-tag v-if="record.rdp">RDP</a-tag></a-space
                    ></template
                ></a-table-column
            >
            <a-table-column :title="t('identity.users.actions')" width="360"
                ><template #default="{ record }"
                    ><a-space wrap
                        ><a-button size="small" @click="edit(record)">{{
                            t("identity.actions.edit")
                        }}</a-button
                        ><a-button size="small" @click="showTelemetryHistory(record)">{{
                            t("nodes.history")
                        }}</a-button
                        ><a-button size="small" danger @click="rotateCredential(record)">{{
                            t("nodes.rotate")
                        }}</a-button
                        ><a-button size="small" danger @click="remove(record)">{{
                            t("identity.actions.delete")
                        }}</a-button></a-space
                    ></template
                ></a-table-column
            >
        </a-table>
    </a-card>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'nodes.edit' : 'nodes.create')"
        :confirm-loading="saving"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('nodes.device')"
                ><a-select
                    v-model:value="form.deviceId"
                    :disabled="!!editing"
                    :options="
                        availableDevices.map(device => ({
                            label: `${device.name} (${device.public_code})`,
                            value: device.id,
                        }))
                    "
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
