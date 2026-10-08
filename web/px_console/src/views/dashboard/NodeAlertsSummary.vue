<script setup lang="ts">
import { useI18n } from "vue-i18n";
import { useDashboardNodeAlerts } from "@/composables/useDashboardNodeAlerts";

const { t } = useI18n();
const { alerts, loading, loadFailed } = useDashboardNodeAlerts();
</script>

<template>
    <router-link
        to="/node-alerts"
        class="node-alert-link"
        data-testid="dashboard-node-alert-summary"
    >
        <a-card hoverable>
            <a-statistic
                :title="t('dashboard.nodeAlerts.title')"
                :value="loading || loadFailed ? '—' : alerts.length"
            />
            <a-typography-text v-if="loadFailed" type="danger">{{
                t("dashboard.nodeAlerts.countFailed")
            }}</a-typography-text>
        </a-card>
    </router-link>
</template>

<style scoped>
.node-alert-link {
    display: block;
    color: inherit;
}
</style>
