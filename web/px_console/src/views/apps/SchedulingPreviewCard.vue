<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { isAxiosError } from "axios";
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
const catalogError = ref(false);
const previewError = ref<
    | "scheduling.failed"
    | "scheduling.sessionExpired"
    | "scheduling.forbidden"
    | "scheduling.applicationMissing"
>();
let previewRequestId = 0;
let catalogRequestId = 0;
let disposed = false;

const eligibleCandidates = computed(
    () => preview.value?.candidates.filter(candidate => candidate.eligible) ?? [],
);
const preferredCandidate = computed(() =>
    eligibleCandidates.value.find(candidate => candidate.rank === 1),
);

const applicationDeployments = computed(() =>
    deployments.value.filter(
        deployment => deployment.application_id === selectedApplicationId.value,
    ),
);

async function loadCatalog(): Promise<void> {
    const requestId = ++catalogRequestId;
    try {
        const [applicationRows, deploymentRows, nodeRows] = await Promise.all([
            listManagedApplications(),
            listManagedDeployments(),
            listManagedNodes(),
        ]);
        if (disposed || requestId !== catalogRequestId) return;
        applications.value = applicationRows;
        deployments.value = deploymentRows;
        nodes.value = nodeRows;
        catalogError.value = false;
        if (
            !applications.value.some(application => application.id === selectedApplicationId.value)
        ) {
            selectedApplicationId.value = applications.value.at(0)?.id || "";
        }
        if (
            !applicationDeployments.value.some(
                deployment => deployment.id === selectedDeploymentId.value,
            )
        ) {
            selectedDeploymentId.value = "";
        }
    } catch {
        if (!disposed && requestId === catalogRequestId) catalogError.value = true;
    }
}

async function runPreview(): Promise<void> {
    if (!selectedApplicationId.value) return;
    const requestId = ++previewRequestId;
    loading.value = true;
    previewError.value = undefined;
    try {
        const result = await previewPlacement(
            selectedApplicationId.value,
            selectedDeploymentId.value || undefined,
        );
        if (!disposed && requestId === previewRequestId) preview.value = result;
    } catch (error) {
        if (disposed || requestId !== previewRequestId) return;
        preview.value = undefined;
        const status = isAxiosError(error) ? error.response?.status : undefined;
        previewError.value =
            status === 401
                ? "scheduling.sessionExpired"
                : status === 403
                  ? "scheduling.forbidden"
                  : status === 404
                    ? "scheduling.applicationMissing"
                    : "scheduling.failed";
    } finally {
        if (!disposed && requestId === previewRequestId) loading.value = false;
    }
}

async function refresh(): Promise<void> {
    await loadCatalog();
    if (!disposed && !loading.value && preview.value && selectedApplicationId.value)
        await runPreview();
}

function resetPreview(): void {
    ++previewRequestId;
    loading.value = false;
    preview.value = undefined;
    previewError.value = undefined;
}

watch(
    selectedApplicationId,
    () => {
        selectedDeploymentId.value = "";
    },
    { flush: "sync" },
);
watch([selectedApplicationId, selectedDeploymentId], resetPreview, { flush: "sync" });

function nodeName(candidate: PlacementCandidate): string {
    return (
        nodes.value.find(node => node.id === candidate.node_id)?.public_host || candidate.node_id
    );
}

function deploymentName(candidate: PlacementCandidate): string {
    const deployment = deployments.value.find(
        deployment => deployment.id === candidate.deployment_id,
    );
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
useManagementRefresh(["applications", "nodes", "deployments", "instances"], refresh);
onBeforeUnmount(() => {
    disposed = true;
    ++previewRequestId;
    ++catalogRequestId;
});
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
        <a-alert
            v-if="catalogError"
            type="error"
            show-icon
            :message="t('scheduling.catalogFailed')"
            style="margin-bottom: 12px"
        >
            <template #action
                ><a-button size="small" @click="loadCatalog">{{
                    t("scheduling.retry")
                }}</a-button></template
            >
        </a-alert>
        <a-alert
            v-if="previewError"
            type="error"
            show-icon
            :message="t(previewError)"
            style="margin-bottom: 12px"
        />
        <p v-else-if="loading" role="status">{{ t("scheduling.checking") }}</p>
        <a-alert
            v-else-if="preview"
            :type="eligibleCandidates.length ? 'success' : 'warning'"
            show-icon
            :message="
                eligibleCandidates.length
                    ? t('scheduling.available', { count: eligibleCandidates.length })
                    : t(
                          preview.candidates.length
                              ? 'scheduling.noneEligible'
                              : 'scheduling.noCandidates',
                      )
            "
            :description="
                preferredCandidate
                    ? t('scheduling.preferred', { node: nodeName(preferredCandidate) })
                    : undefined
            "
            style="margin-bottom: 12px"
        />
        <p v-else role="status">{{ t("scheduling.idle") }}</p>
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
