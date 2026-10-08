<script setup lang="ts">
import { computed, ref } from "vue";
import { useI18n } from "vue-i18n";
import type { ManagedApplication } from "@/model/managed_application_api";
import type { ManagedDeployment } from "@/model/managed_deployment_api";
import type { ManagedNode } from "@/model/managed_node_api";
import type { PlacementPreview } from "@/model/managed_scheduling_api";
import { schedulingHeadroom, type InstanceSummary } from "@/model/dashboard_runtime";
import ApplicationDeploymentsModal from "@/views/apps/ApplicationDeploymentsModal.vue";

const props = defineProps<{
    applications: ManagedApplication[];
    deployments: ManagedDeployment[];
    nodes: ManagedNode[];
    instances: InstanceSummary[];
    previews: Record<string, PlacementPreview | undefined>;
    current: boolean;
    observedAt: number;
}>();
const { t } = useI18n();
const selectedApplicationId = ref<string>();
const selectedApplication = computed(() =>
    props.applications.find(application => application.id === selectedApplicationId.value),
);
const columns = computed(() => [
    { title: t("dashboard.runtime.application"), key: "application" },
    { title: t("dashboard.runtime.machines"), key: "machines", width: 150 },
    { title: t("dashboard.runtime.running"), key: "running", width: 140 },
    { title: t("dashboard.runtime.pending"), key: "pending", width: 150 },
    { title: t("dashboard.runtime.headroom"), key: "headroom", width: 190 },
]);
function count(applicationId: string, running: boolean): number {
    return props.instances
        .filter(
            instance =>
                instance.application_id === applicationId &&
                (instance.state === "running") === running,
        )
        .reduce((total, instance) => total + instance.count, 0);
}
function machineCount(applicationId: string): number {
    return new Set(
        props.deployments
            .filter(deployment => deployment.application_id === applicationId)
            .map(deployment => deployment.node_id),
    ).size;
}
function headroom(application: ManagedApplication): number | undefined {
    const preview = props.previews[application.id];
    const age = props.observedAt - Date.parse(preview?.evaluated_at ?? "");
    if (!props.current || !preview || !Number.isFinite(age) || age < -5_000 || age > 30_000)
        return undefined;
    return schedulingHeadroom(
        preview,
        props.nodes,
        props.instances,
        application.spec.launch.kind === "rdp",
    );
}
</script>

<template>
    <a-card :title="t('dashboard.runtime.applicationsTitle')" class="application-runtime">
        <template #extra
            ><RouterLink to="/apps">{{ t("dashboard.runtime.manageApps") }}</RouterLink></template
        >
        <a-table
            :columns="columns"
            :data-source="applications"
            row-key="id"
            size="middle"
            :pagination="applications.length > 8 ? { pageSize: 8 } : false"
            :scroll="{ x: 850 }"
        >
            <template #bodyCell="{ column, record }">
                <template v-if="column.key === 'application'">
                    <a-button
                        type="link"
                        class="application-name"
                        @click="selectedApplicationId = record.id"
                        >{{ record.spec.name }}</a-button
                    >
                    <a-tag v-if="record.spec.disabled">{{
                        t("nodes.operationalStates.disabled")
                    }}</a-tag>
                </template>
                <a-button
                    v-else-if="column.key === 'machines'"
                    type="link"
                    @click="selectedApplicationId = record.id"
                    >{{ machineCount(record.id) }}</a-button
                >
                <template v-else-if="column.key === 'running'">{{
                    current
                        ? t("dashboard.runtime.streamCount", { count: count(record.id, true) })
                        : "—"
                }}</template>
                <template v-else-if="column.key === 'pending'">{{
                    current ? count(record.id, false) : "—"
                }}</template>
                <template v-else-if="column.key === 'headroom'">
                    {{
                        headroom(record) == null
                            ? "—"
                            : t("dashboard.runtime.streamCount", { count: headroom(record) })
                    }}
                </template>
            </template>
        </a-table>
        <div class="capacity-note">{{ t("dashboard.runtime.capacityNotice") }}</div>
        <ApplicationDeploymentsModal
            v-if="selectedApplication"
            :key="selectedApplication.id"
            :application="selectedApplication"
            @close="selectedApplicationId = undefined"
        />
    </a-card>
</template>

<style scoped>
.application-name {
    padding: 0;
    height: auto;
    white-space: normal;
    text-align: start;
}
.capacity-note {
    margin-top: 12px;
    opacity: 0.65;
    font-size: 12px;
}
</style>
