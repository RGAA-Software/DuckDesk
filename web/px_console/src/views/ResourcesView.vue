<script setup lang="ts">
import { computed } from "vue";
import { useI18n } from "vue-i18n";
import { useDashboardResources } from "@/composables/useDashboardResources";
import NodeAlertsSummary from "@/views/dashboard/NodeAlertsSummary.vue";
import NodeResourcesCard from "@/views/dashboard/NodeResourcesCard.vue";
import ApplicationRuntimeCard from "@/views/dashboard/ApplicationRuntimeCard.vue";

const { t } = useI18n();
const {
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
} = useDashboardResources();
const statistics = computed(() => [
    { key: "devices", value: devices.value.length },
    { key: "users", value: users.value },
    { key: "applications", value: applications.value.length },
    { key: "deployments", value: deployments.value.length },
    {
        key: "nodes",
        value: `${nodes.value.filter(node => node.fresh && !node.disabled).length}/${nodes.value.length}`,
    },
    {
        key: "activeSessions",
        value: sessions.value.filter(session => !session.closed_at && session.state !== "closed")
            .length,
    },
]);
</script>

<template>
    <div class="resource-overview">
        <div class="overview-toolbar">
            <span>{{ t("dashboard.runtime.refreshNotice") }}</span>
            <a-button :loading="loading" @click="refresh">{{ t("dashboard.refresh") }}</a-button>
        </div>
        <a-alert
            v-if="loadFailed || (!current && !loading)"
            type="warning"
            show-icon
            :message="t('dashboard.runtime.loadFailed')"
        />
        <div class="summary-grid">
            <a-card v-for="statistic in statistics" :key="statistic.key">
                <a-statistic
                    :title="t(`dashboard.${statistic.key}`)"
                    :value="current ? statistic.value : '—'"
                />
            </a-card>
            <NodeAlertsSummary />
        </div>
        <NodeResourcesCard
            :nodes="nodes"
            :devices="devices"
            :instances="instances"
            :current="current"
            :observed-at="observedAt"
        />
        <ApplicationRuntimeCard
            :applications="applications"
            :deployments="deployments"
            :nodes="nodes"
            :instances="instances"
            :previews="previews"
            :current="current"
            :observed-at="observedAt"
        />
    </div>
</template>

<style scoped>
.resource-overview {
    display: grid;
    gap: 16px;
    min-width: 0;
}
.overview-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
}
.overview-toolbar > span {
    opacity: 0.65;
    font-size: 12px;
}
.summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 16px;
}
.resource-overview > *,
.summary-grid > * {
    min-width: 0;
}
</style>
