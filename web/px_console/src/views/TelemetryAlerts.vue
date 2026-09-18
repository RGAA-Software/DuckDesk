<script setup lang="ts">
import { onMounted, ref } from "vue";
import { message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import {
    acknowledgeTelemetryAlert,
    configureTelemetryAlertPolicy,
    getTelemetryAlert,
    getTelemetryAlertPolicy,
    listTelemetryAlerts,
    type TelemetryAlertEvent,
    type TelemetryAlertMetric,
    type TelemetryAlertPolicy,
    type TelemetryAlertSeverity,
    type TelemetryAlertState,
} from "@/model/telemetry_alert_api";

const { t } = useI18n();
const alerts = ref<TelemetryAlertEvent[]>([]);
const loading = ref(false);
const detailLoading = ref(false);
const detailOpen = ref(false);
const selectedAlert = ref<TelemetryAlertEvent>();
const policyOpen = ref(false);
const policyLoading = ref(false);
const policySaving = ref(false);
const alertPolicy = ref<TelemetryAlertPolicy>();
const nodeId = ref("");
const metric = ref<TelemetryAlertMetric>();
const severity = ref<TelemetryAlertSeverity>();
const state = ref<TelemetryAlertState>();

async function refresh() {
    loading.value = true;
    try {
        alerts.value = await listTelemetryAlerts({
            nodeId: nodeId.value.trim() || undefined,
            metric: metric.value,
            severity: severity.value,
            state: state.value,
        });
    } catch {
        message.error(t("telemetryAlerts.loadFailed"));
    } finally {
        loading.value = false;
    }
}

async function showDetail(event: TelemetryAlertEvent) {
    detailOpen.value = true;
    detailLoading.value = true;
    try {
        selectedAlert.value = await getTelemetryAlert(event.id);
    } catch {
        detailOpen.value = false;
        message.error(t("telemetryAlerts.loadFailed"));
    } finally {
        detailLoading.value = false;
    }
}

async function acknowledge(event: TelemetryAlertEvent) {
    try {
        const updated = await acknowledgeTelemetryAlert(event);
        alerts.value = alerts.value.map(candidate =>
            candidate.id === updated.id ? updated : candidate,
        );
        if (selectedAlert.value?.id === updated.id) selectedAlert.value = updated;
        message.success(t("telemetryAlerts.acknowledged"));
    } catch {
        message.error(t("telemetryAlerts.acknowledgeFailed"));
    }
}

async function showPolicy() {
    const selectedNodeId = nodeId.value.trim();
    if (!selectedNodeId) return;
    policyOpen.value = true;
    policyLoading.value = true;
    try {
        alertPolicy.value = await getTelemetryAlertPolicy(selectedNodeId);
    } catch {
        policyOpen.value = false;
        message.error(t("telemetryAlerts.policyLoadFailed"));
    } finally {
        policyLoading.value = false;
    }
}

async function savePolicy() {
    if (!alertPolicy.value) return;
    policySaving.value = true;
    try {
        alertPolicy.value = await configureTelemetryAlertPolicy(alertPolicy.value);
        policyOpen.value = false;
        message.success(t("telemetryAlerts.policySaved"));
    } catch {
        message.error(t("telemetryAlerts.policySaveFailed"));
    } finally {
        policySaving.value = false;
    }
}

function percent(value: number) {
    return `${(value / 10).toFixed(1)}%`;
}

function timestamp(value: string | null) {
    return value ? new Date(value).toLocaleString() : "-";
}

function severityColor(value: TelemetryAlertSeverity) {
    return value === "critical" ? "error" : "warning";
}

function stateColor(value: TelemetryAlertState) {
    if (value === "recovered") return "success";
    if (value === "acknowledged") return "processing";
    return "warning";
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('telemetryAlerts.title')">
        <template #extra
            ><a-button @click="refresh">{{ t("dashboard.refresh") }}</a-button></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('telemetryAlerts.notice')"
            style="margin-bottom: 12px"
        />
        <a-space wrap style="margin-bottom: 12px">
            <a-input
                v-model:value="nodeId"
                allow-clear
                :placeholder="t('telemetryAlerts.nodeFilter')"
                style="width: 300px"
            />
            <a-select
                v-model:value="metric"
                allow-clear
                :placeholder="t('telemetryAlerts.metric')"
                style="width: 150px"
            >
                <a-select-option
                    v-for="value in ['cpu', 'memory', 'disk', 'gpu']"
                    :key="value"
                    :value="value"
                >
                    {{ t(`telemetryAlerts.metrics.${value}`) }}
                </a-select-option>
            </a-select>
            <a-select
                v-model:value="severity"
                allow-clear
                :placeholder="t('telemetryAlerts.severity')"
                style="width: 150px"
            >
                <a-select-option
                    v-for="value in ['warning', 'critical']"
                    :key="value"
                    :value="value"
                >
                    {{ t(`telemetryAlerts.severities.${value}`) }}
                </a-select-option>
            </a-select>
            <a-select
                v-model:value="state"
                allow-clear
                :placeholder="t('telemetryAlerts.state')"
                style="width: 150px"
            >
                <a-select-option
                    v-for="value in ['active', 'acknowledged', 'recovered']"
                    :key="value"
                    :value="value"
                >
                    {{ t(`telemetryAlerts.states.${value}`) }}
                </a-select-option>
            </a-select>
            <a-button type="primary" @click="refresh">{{ t("telemetryAlerts.filter") }}</a-button>
            <a-button :disabled="!nodeId.trim()" @click="showPolicy">
                {{ t("telemetryAlerts.policy") }}
            </a-button>
        </a-space>
        <a-table
            :data-source="alerts"
            row-key="id"
            :loading="loading"
            :pagination="{ pageSize: 20 }"
        >
            <a-table-column :title="t('telemetryAlerts.updatedAt')">
                <template #default="{ record }">{{ timestamp(record.updated_at) }}</template>
            </a-table-column>
            <a-table-column :title="t('telemetryAlerts.node')" data-index="node_id" />
            <a-table-column :title="t('telemetryAlerts.metric')">
                <template #default="{ record }">{{
                    t(`telemetryAlerts.metrics.${record.metric}`)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('telemetryAlerts.resource')" data-index="resource_name" />
            <a-table-column :title="t('telemetryAlerts.severity')">
                <template #default="{ record }">
                    <a-tag :color="severityColor(record.severity)">
                        {{ t(`telemetryAlerts.severities.${record.severity}`) }}
                    </a-tag>
                </template>
            </a-table-column>
            <a-table-column :title="t('telemetryAlerts.state')">
                <template #default="{ record }">
                    <a-tag :color="stateColor(record.state)">
                        {{ t(`telemetryAlerts.states.${record.state}`) }}
                    </a-tag>
                </template>
            </a-table-column>
            <a-table-column :title="t('telemetryAlerts.latest')">
                <template #default="{ record }">{{
                    percent(record.latest_value_per_mille)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('telemetryAlerts.actions')">
                <template #default="{ record }">
                    <a-space>
                        <a-button size="small" @click="showDetail(record)">{{
                            t("telemetryAlerts.detail")
                        }}</a-button>
                        <a-button
                            v-if="record.state === 'active'"
                            size="small"
                            type="primary"
                            @click="acknowledge(record)"
                        >
                            {{ t("telemetryAlerts.acknowledge") }}
                        </a-button>
                    </a-space>
                </template>
            </a-table-column>
            <template #emptyText>{{ t("telemetryAlerts.empty") }}</template>
        </a-table>
    </a-card>

    <a-drawer v-model:open="detailOpen" :title="t('telemetryAlerts.detailTitle')" width="560">
        <a-spin :spinning="detailLoading">
            <a-descriptions v-if="selectedAlert" bordered :column="1">
                <a-descriptions-item :label="t('telemetryAlerts.node')">{{
                    selectedAlert.node_id
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.resource')">
                    {{ selectedAlert.resource_name }} ({{ selectedAlert.resource_key }})
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.threshold')">
                    {{ percent(selectedAlert.threshold_per_mille) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.firstLatestPeak')">
                    {{ percent(selectedAlert.first_value_per_mille) }} /
                    {{ percent(selectedAlert.latest_value_per_mille) }} /
                    {{ percent(selectedAlert.peak_value_per_mille) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.occurrences')">
                    {{ selectedAlert.occurrence_count }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.firstSampledAt')">
                    {{ timestamp(selectedAlert.first_sampled_at) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.lastSampledAt')">
                    {{ timestamp(selectedAlert.last_sampled_at) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.acknowledgedAt')">
                    {{ timestamp(selectedAlert.acknowledged_at) }}
                </a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.recoveredAt')">
                    {{ timestamp(selectedAlert.recovered_at) }}
                </a-descriptions-item>
            </a-descriptions>
        </a-spin>
    </a-drawer>

    <a-modal
        v-model:open="policyOpen"
        :title="t('telemetryAlerts.policyTitle')"
        :confirm-loading="policySaving"
        width="720px"
        @ok="savePolicy"
    >
        <a-spin :spinning="policyLoading">
            <a-alert
                type="info"
                show-icon
                :message="t('telemetryAlerts.policyNotice')"
                style="margin-bottom: 12px"
            />
            <a-form v-if="alertPolicy" layout="vertical">
                <a-row :gutter="12">
                    <a-col :span="12">
                        <a-form-item :label="t('telemetryAlerts.metrics.cpu')">
                            <a-space>
                                <a-input-number
                                    v-model:value="alertPolicy.cpu_warning_per_mille"
                                    :min="1"
                                    :max="999"
                                />
                                <span>{{ t("telemetryAlerts.warningPerMille") }}</span>
                                <a-input-number
                                    v-model:value="alertPolicy.cpu_critical_per_mille"
                                    :min="2"
                                    :max="1000"
                                />
                                <span>{{ t("telemetryAlerts.criticalPerMille") }}</span>
                            </a-space>
                        </a-form-item>
                    </a-col>
                    <a-col :span="12">
                        <a-form-item :label="t('telemetryAlerts.metrics.memory')">
                            <a-space>
                                <a-input-number
                                    v-model:value="alertPolicy.memory_warning_per_mille"
                                    :min="1"
                                    :max="999"
                                />
                                <span>{{ t("telemetryAlerts.warningPerMille") }}</span>
                                <a-input-number
                                    v-model:value="alertPolicy.memory_critical_per_mille"
                                    :min="2"
                                    :max="1000"
                                />
                                <span>{{ t("telemetryAlerts.criticalPerMille") }}</span>
                            </a-space>
                        </a-form-item>
                    </a-col>
                    <a-col :span="12">
                        <a-form-item :label="t('telemetryAlerts.metrics.disk')">
                            <a-space>
                                <a-input-number
                                    v-model:value="alertPolicy.disk_warning_per_mille"
                                    :min="1"
                                    :max="999"
                                />
                                <span>{{ t("telemetryAlerts.warningPerMille") }}</span>
                                <a-input-number
                                    v-model:value="alertPolicy.disk_critical_per_mille"
                                    :min="2"
                                    :max="1000"
                                />
                                <span>{{ t("telemetryAlerts.criticalPerMille") }}</span>
                            </a-space>
                        </a-form-item>
                    </a-col>
                    <a-col :span="12">
                        <a-form-item :label="t('telemetryAlerts.metrics.gpu')">
                            <a-space>
                                <a-input-number
                                    v-model:value="alertPolicy.gpu_warning_per_mille"
                                    :min="1"
                                    :max="999"
                                />
                                <span>{{ t("telemetryAlerts.warningPerMille") }}</span>
                                <a-input-number
                                    v-model:value="alertPolicy.gpu_critical_per_mille"
                                    :min="2"
                                    :max="1000"
                                />
                                <span>{{ t("telemetryAlerts.criticalPerMille") }}</span>
                            </a-space>
                        </a-form-item>
                    </a-col>
                </a-row>
                <a-row :gutter="12">
                    <a-col :span="8">
                        <a-form-item :label="t('telemetryAlerts.triggerSamples')">
                            <a-input-number
                                v-model:value="alertPolicy.trigger_samples"
                                :min="1"
                                :max="60"
                            />
                        </a-form-item>
                    </a-col>
                    <a-col :span="8">
                        <a-form-item :label="t('telemetryAlerts.recoverySamples')">
                            <a-input-number
                                v-model:value="alertPolicy.recovery_samples"
                                :min="1"
                                :max="60"
                            />
                        </a-form-item>
                    </a-col>
                    <a-col :span="8">
                        <a-form-item :label="t('telemetryAlerts.hysteresisPerMille')">
                            <a-input-number
                                v-model:value="alertPolicy.recovery_hysteresis_per_mille"
                                :min="1"
                                :max="250"
                            />
                        </a-form-item>
                    </a-col>
                </a-row>
            </a-form>
        </a-spin>
    </a-modal>
</template>
