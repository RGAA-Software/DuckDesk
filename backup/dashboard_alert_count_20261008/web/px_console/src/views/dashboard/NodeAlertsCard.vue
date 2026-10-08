<script setup lang="ts">
import { computed, ref } from "vue";
import { useI18n } from "vue-i18n";
import {
    useDashboardNodeAlerts,
    type DashboardNodeAlert,
} from "@/composables/useDashboardNodeAlerts";

const { t } = useI18n();
const { alerts, loading, loadFailed, truncated, refresh } = useDashboardNodeAlerts();
const selectedAlert = ref<DashboardNodeAlert>();
const detailOpen = ref(false);
const visibleAlerts = computed(() => alerts.value.slice(0, 10));

function timestamp(value: string | null) {
    return value ? new Date(value).toLocaleString() : t("dashboard.nodeAlerts.neverReported");
}
function reason(alert: DashboardNodeAlert) {
    if (!alert.event) return t("dashboard.nodeAlerts.offlineReason");
    return t("dashboard.nodeAlerts.thresholdReason", {
        metric: t(`telemetryAlerts.metrics.${alert.event.metric}`),
        resource: alert.event.resource_name,
        value: (alert.event.latest_value_per_mille / 10).toFixed(1),
        threshold: (alert.event.threshold_per_mille / 10).toFixed(1),
    });
}
function showDetail(alert: DashboardNodeAlert) {
    selectedAlert.value = alert;
    detailOpen.value = true;
}
</script>

<template>
    <a-card :title="t('dashboard.nodeAlerts.title')" data-testid="dashboard-node-alerts">
        <template #extra>
            <a-space>
                <router-link to="/telemetry-alerts">{{
                    t("dashboard.nodeAlerts.viewAll")
                }}</router-link>
                <a-button :loading="loading" @click="refresh">{{
                    t("dashboard.refresh")
                }}</a-button>
            </a-space>
        </template>
        <a-alert
            v-if="loadFailed"
            type="error"
            show-icon
            :message="t('dashboard.nodeAlerts.loadFailed')"
        />
        <template v-else>
            <a-typography-paragraph type="secondary">{{
                t("dashboard.nodeAlerts.notice")
            }}</a-typography-paragraph>
            <a-table
                :data-source="visibleAlerts"
                row-key="id"
                :loading="loading"
                :pagination="false"
                :scroll="{ x: 850 }"
            >
                <a-table-column :title="t('telemetryAlerts.node')">
                    <template #default="{ record }">{{ record.nodeName }}</template>
                </a-table-column>
                <a-table-column :title="t('telemetryAlerts.severity')">
                    <template #default="{ record }">
                        <a-tag :color="record.severity === 'critical' ? 'error' : 'warning'">{{
                            t(`telemetryAlerts.severities.${record.severity}`)
                        }}</a-tag>
                    </template>
                </a-table-column>
                <a-table-column :title="t('dashboard.nodeAlerts.reason')">
                    <template #default="{ record }">{{ reason(record) }}</template>
                </a-table-column>
                <a-table-column :title="t('telemetryAlerts.state')">
                    <template #default="{ record }">{{
                        record.event
                            ? t(`telemetryAlerts.states.${record.event.state}`)
                            : t("dashboard.nodeAlerts.offline")
                    }}</template>
                </a-table-column>
                <a-table-column :title="t('dashboard.nodeAlerts.lastObserved')">
                    <template #default="{ record }">{{ timestamp(record.lastSeen) }}</template>
                </a-table-column>
                <a-table-column :title="t('telemetryAlerts.actions')">
                    <template #default="{ record }">
                        <a-button size="small" @click="showDetail(record)">{{
                            t("telemetryAlerts.detail")
                        }}</a-button>
                    </template>
                </a-table-column>
                <template #emptyText>{{
                    loading ? t("dashboard.nodeAlerts.loading") : t("dashboard.nodeAlerts.empty")
                }}</template>
            </a-table>
            <a-typography-paragraph
                v-if="truncated || alerts.length > 10"
                type="secondary"
                style="margin-top: 12px"
            >
                {{ t("dashboard.nodeAlerts.limited") }}
            </a-typography-paragraph>
        </template>
    </a-card>
    <a-drawer v-model:open="detailOpen" :title="t('telemetryAlerts.detailTitle')" width="560">
        <a-descriptions v-if="selectedAlert" bordered :column="1">
            <a-descriptions-item :label="t('telemetryAlerts.node')">{{
                selectedAlert.nodeName
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.nodeAlerts.nodeId')">{{
                selectedAlert.nodeId
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.nodeAlerts.reason')">{{
                reason(selectedAlert)
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('dashboard.nodeAlerts.lastObserved')">{{
                timestamp(selectedAlert.lastSeen)
            }}</a-descriptions-item>
            <template v-if="selectedAlert.event">
                <a-descriptions-item :label="t('telemetryAlerts.firstSampledAt')">{{
                    timestamp(selectedAlert.event.first_sampled_at)
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.occurrences')">{{
                    selectedAlert.event.occurrence_count
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('telemetryAlerts.acknowledgedAt')">{{
                    selectedAlert.event.acknowledged_at
                        ? timestamp(selectedAlert.event.acknowledged_at)
                        : "-"
                }}</a-descriptions-item>
            </template>
        </a-descriptions>
    </a-drawer>
</template>
