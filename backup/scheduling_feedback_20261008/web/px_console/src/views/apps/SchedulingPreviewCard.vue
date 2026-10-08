<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import { listManagedApplications, type ManagedApplication } from "@/model/managed_application_api";
import { listManagedDeployments, type ManagedDeployment } from "@/model/managed_deployment_api";
import { useManagementRefresh } from "@/model/management_events.ts";
import { listManagedNodes, type ManagedNode } from "@/model/managed_node_api";
import {
    previewPlacement,
    type PlacementCandidate,
    type PlacementPreview,
} from "@/model/managed_scheduling_api";

const { t } = useI18n();
const applications = ref<ManagedApplication[]>([]);
const deployments = ref<ManagedDeployment[]>([]);
const nodes = ref<ManagedNode[]>([]);
const selectedApplicationId = ref("");
const selectedDeploymentId = ref("");
const preview = ref<PlacementPreview>();
const loading = ref(false);

const applicationDeployments = computed(() =>
    deployments.value.filter(
        deployment => deployment.application_id === selectedApplicationId.value,
    ),
);

async function loadCatalog(): Promise<void> {
    [applications.value, deployments.value, nodes.value] = await Promise.all([
        listManagedApplications(),
        listManagedDeployments(),
        listManagedNodes(),
    ]);
    if (!applications.value.some(application => application.id === selectedApplicationId.value)) {
        selectedApplicationId.value = applications.value.at(0)?.id || "";
        selectedDeploymentId.value = "";
    }
}

async function runPreview(): Promise<void> {
    if (!selectedApplicationId.value) return;
    loading.value = true;
    preview.value = await previewPlacement(
        selectedApplicationId.value,
        selectedDeploymentId.value || undefined,
    ).finally(() => {
        loading.value = false;
    });
}

async function refresh(): Promise<void> {
    await loadCatalog();
    if (preview.value && selectedApplicationId.value) await runPreview();
}

function selectApplication(): void {
    selectedDeploymentId.value = "";
    preview.value = undefined;
}

function nodeName(candidate: PlacementCandidate): string {
    return (
        nodes.value.find(node => node.id === candidate.node_id)?.public_host || candidate.node_id
    );
}

function deploymentName(candidate: PlacementCandidate): string {
    const deployment = deployments.value.find(item => item.id === candidate.deployment_id);
    return deployment
        ? `${deployment.kind} · ${candidate.deployment_id.slice(0, 8)}`
        : candidate.deployment_id;
}

function pressure(value: number | null): string {
    return value === null ? t("scheduling.unknown") : `${(value / 10).toFixed(1)}%`;
}

function memory(value: number | null): string {
    return value === null ? t("scheduling.unknown") : `${Math.round(value / (1024 * 1024))} MiB`;
}

onMounted(loadCatalog);
useManagementRefresh(["nodes", "deployments", "instances"], refresh);
</script>

<template>
    <a-card :title="t('scheduling.title')">
        <a-alert
            type="info"
            show-icon
            :message="t('scheduling.notice')"
            style="margin-bottom: 12px"
        />
        <a-space wrap style="margin-bottom: 12px">
            <a-select
                v-model:value="selectedApplicationId"
                style="min-width: 220px"
                :placeholder="t('scheduling.application')"
                :options="
                    applications.map(application => ({
                        label: application.spec.name,
                        value: application.id,
                    }))
                "
                @change="selectApplication"
            />
            <a-select
                v-model:value="selectedDeploymentId"
                style="min-width: 240px"
                :disabled="!selectedApplicationId"
                :options="[
                    { label: t('scheduling.allDeployments'), value: '' },
                    ...applicationDeployments.map(deployment => ({
                        label: `${deployment.kind} · ${deployment.id.slice(0, 8)}`,
                        value: deployment.id,
                    })),
                ]"
            />
            <a-button
                type="primary"
                :disabled="!selectedApplicationId"
                :loading="loading"
                @click="runPreview"
            >
                {{ t("scheduling.preview") }}
            </a-button>
            <span v-if="preview" class="text-gray-500">{{
                new Date(preview.evaluated_at).toLocaleString()
            }}</span>
        </a-space>
        <a-table
            v-if="preview"
            :data-source="preview.candidates"
            :loading="loading"
            :pagination="false"
            :locale="{ emptyText: t('scheduling.noCandidates') }"
            :row-key="
                (candidate: PlacementCandidate) =>
                    `${candidate.deployment_id}:${candidate.gpu_key || 'none'}`
            "
        >
            <a-table-column :title="t('scheduling.rank')" data-index="rank" />
            <a-table-column :title="t('scheduling.deployment')">
                <template #default="{ record }">{{ deploymentName(record) }}</template>
            </a-table-column>
            <a-table-column :title="t('scheduling.node')">
                <template #default="{ record }">{{ nodeName(record) }}</template>
            </a-table-column>
            <a-table-column :title="t('scheduling.gpu')">
                <template #default="{ record }">{{
                    record.gpu_key || t("scheduling.notApplicable")
                }}</template>
            </a-table-column>
            <a-table-column :title="t('scheduling.pressure')">
                <template #default="{ record }">
                    {{ pressure(record.dominant_pressure_per_mille) }} /
                    {{ pressure(record.average_pressure_per_mille) }}
                </template>
            </a-table-column>
            <a-table-column :title="t('scheduling.slots')">
                <template #default="{ record }"
                    >{{ record.node_slots }} / {{ record.deployment_slots }}</template
                >
            </a-table-column>
            <a-table-column :title="t('scheduling.headroom')">
                <template #default="{ record }">
                    {{ memory(record.gpu_memory_headroom_bytes) }} ·
                    {{ pressure(record.gpu_compute_headroom_per_mille) }} ·
                    {{ pressure(record.gpu_encoder_headroom_per_mille) }}
                </template>
            </a-table-column>
            <a-table-column :title="t('scheduling.decision')">
                <template #default="{ record }">
                    <a-tag v-if="record.eligible" color="green">{{
                        t("scheduling.eligible")
                    }}</a-tag>
                    <a-space v-else wrap>
                        <a-tag v-for="reason in record.rejection_reasons" :key="reason" color="red">
                            {{ t(`scheduling.reasons.${reason}`) }}
                        </a-tag>
                    </a-space>
                </template>
            </a-table-column>
        </a-table>
    </a-card>
</template>
