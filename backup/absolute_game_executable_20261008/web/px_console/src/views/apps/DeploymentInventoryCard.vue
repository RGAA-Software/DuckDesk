<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import { message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { listManagedApplications, type ManagedApplication } from "@/model/managed_application_api";
import { listManagedNodes, type ManagedNode } from "@/model/managed_node_api";
import {
    configureManagedDeployment,
    createManagedDeployment,
    listManagedDeployments,
    type DeploymentConfiguration,
    type ManagedDeployment,
} from "@/model/managed_deployment_api";

const { t } = useI18n();
const deployments = ref<ManagedDeployment[]>([]);
const applications = ref<ManagedApplication[]>([]);
const nodes = ref<ManagedNode[]>([]);
const loading = ref(false);
const saving = ref(false);
const editorOpen = ref(false);
const editing = ref<ManagedDeployment>();
const form = reactive({
    applicationId: "",
    nodeId: "",
    installRoot: "",
    gpuKey: "",
    gpuMemoryMiB: 1024,
    gpuComputePercent: 20,
    gpuEncoderPercent: 25,
    gpuMemoryReserveMiB: 512,
    gpuComputeLimitPercent: 90,
    gpuEncoderLimitPercent: 90,
    capacity: 1,
    disabled: false,
});

const selectedApplication = computed(() =>
    applications.value.find(application => application.id === form.applicationId),
);
const selectedKind = computed(
    () => selectedApplication.value?.spec.launch.kind || editing.value?.kind || "game_hook",
);

async function refresh() {
    loading.value = true;
    try {
        [deployments.value, applications.value, nodes.value] = await Promise.all([
            listManagedDeployments(),
            listManagedApplications(),
            listManagedNodes(),
        ]);
    } finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, {
        applicationId: applications.value.find(application => !application.spec.disabled)?.id || "",
        nodeId: nodes.value.find(node => !node.disabled)?.id || "",
        installRoot: "",
        gpuKey: "",
        gpuMemoryMiB: 1024,
        gpuComputePercent: 20,
        gpuEncoderPercent: 25,
        gpuMemoryReserveMiB: 512,
        gpuComputeLimitPercent: 90,
        gpuEncoderLimitPercent: 90,
        capacity: 1,
        disabled: false,
    });
    editorOpen.value = true;
}

function edit(deployment: ManagedDeployment) {
    editing.value = deployment;
    Object.assign(form, {
        applicationId: deployment.application_id,
        nodeId: deployment.node_id,
        installRoot: deployment.install_root || "",
        gpuKey: deployment.gpu_key || "",
        gpuMemoryMiB: (deployment.gpu_memory_bytes || 0) / (1024 * 1024),
        gpuComputePercent: (deployment.gpu_compute_per_mille || 0) / 10,
        gpuEncoderPercent: (deployment.gpu_encoder_per_mille || 0) / 10,
        gpuMemoryReserveMiB: (deployment.gpu_memory_reserve_bytes || 0) / (1024 * 1024),
        gpuComputeLimitPercent: (deployment.gpu_compute_limit_per_mille || 0) / 10,
        gpuEncoderLimitPercent: (deployment.gpu_encoder_limit_per_mille || 0) / 10,
        capacity: deployment.capacity,
        disabled: deployment.disabled,
    });
    editorOpen.value = true;
}

function configuration(): DeploymentConfiguration {
    const kind = selectedKind.value;
    return {
        target: kind === "game_hook" ? { kind, install_root: form.installRoot } : { kind },
        gpu_key: kind === "rdp" ? null : form.gpuKey.trim() || null,
        gpu_profile:
            kind === "rdp"
                ? null
                : {
                      memory_bytes: form.gpuMemoryMiB * 1024 * 1024,
                      compute_per_mille: form.gpuComputePercent * 10,
                      encoder_per_mille: form.gpuEncoderPercent * 10,
                      memory_reserve_bytes: form.gpuMemoryReserveMiB * 1024 * 1024,
                      compute_limit_per_mille: form.gpuComputeLimitPercent * 10,
                      encoder_limit_per_mille: form.gpuEncoderLimitPercent * 10,
                  },
        capacity: kind === "rdp" ? 1 : form.capacity,
        disabled: form.disabled,
    };
}

async function save() {
    if (!form.applicationId || !form.nodeId) {
        message.error(t("deployments.validation.selection"));
        return;
    }
    if (selectedKind.value === "game_hook" && !form.installRoot.trim()) {
        message.error(t("deployments.validation.installRoot"));
        return;
    }
    if (
        selectedKind.value !== "rdp" &&
        (form.gpuComputeLimitPercent < form.gpuComputePercent ||
            form.gpuEncoderLimitPercent < form.gpuEncoderPercent)
    ) {
        message.error(t("deployments.validation.gpuLimits"));
        return;
    }
    saving.value = true;
    try {
        if (editing.value) {
            await configureManagedDeployment(editing.value, configuration());
        } else {
            await createManagedDeployment(form.applicationId, form.nodeId, configuration());
        }
        editorOpen.value = false;
        message.success(t("deployments.messages.saved"));
        await refresh();
    } finally {
        saving.value = false;
    }
}

function applicationName(deployment: ManagedDeployment) {
    return (
        applications.value.find(application => application.id === deployment.application_id)?.spec
            .name || deployment.application_id
    );
}

function nodeName(deployment: ManagedDeployment) {
    return (
        nodes.value.find(node => node.id === deployment.node_id)?.public_host || deployment.node_id
    );
}

onMounted(refresh);
useManagementRefresh(["deployments"], refresh);
</script>

<template>
    <a-card :title="t('deployments.title')">
        <template #extra
            ><a-button
                type="primary"
                :disabled="applications.length === 0 || nodes.length === 0"
                @click="create"
                >{{ t("deployments.create") }}</a-button
            ></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('deployments.statusNotice')"
            style="margin-bottom: 12px"
        />
        <a-table :data-source="deployments" row-key="id" :loading="loading" :pagination="false">
            <a-table-column :title="t('deployments.application')"
                ><template #default="{ record }">{{
                    applicationName(record)
                }}</template></a-table-column
            >
            <a-table-column :title="t('deployments.node')"
                ><template #default="{ record }">{{ nodeName(record) }}</template></a-table-column
            >
            <a-table-column :title="t('applications.kind')"
                ><template #default="{ record }">{{
                    t(`applications.kinds.${record.kind}`)
                }}</template></a-table-column
            >
            <a-table-column :title="t('deployments.capacity')" data-index="capacity" />
            <a-table-column :title="t('deployments.gpuBudget')"
                ><template #default="{ record }">
                    <span v-if="record.gpu_memory_bytes !== null">{{
                        t("deployments.gpuBudgetValue", {
                            memory: record.gpu_memory_bytes / (1024 * 1024),
                            compute: record.gpu_compute_per_mille / 10,
                            encoder: record.gpu_encoder_per_mille / 10,
                        })
                    }}</span>
                    <span v-else>{{ t("deployments.notApplicable") }}</span>
                </template></a-table-column
            >
            <a-table-column :title="t('deployments.observed')"
                ><template #default="{ record }"
                    ><a-tag
                        :color="
                            record.observed_state === 'ready'
                                ? 'green'
                                : record.observed_state === 'failed'
                                  ? 'red'
                                  : 'default'
                        "
                        >{{ record.observed_state
                        }}<template v-if="record.observed_reason">
                            / {{ record.observed_reason }}</template
                        ></a-tag
                    ></template
                ></a-table-column
            >
            <a-table-column :title="t('identity.users.status')"
                ><template #default="{ record }">{{
                    t(record.disabled ? "identity.states.disabled" : "identity.states.active")
                }}</template></a-table-column
            >
            <a-table-column :title="t('identity.users.actions')"
                ><template #default="{ record }"
                    ><a-button size="small" @click="edit(record)">{{
                        t("identity.actions.edit")
                    }}</a-button></template
                ></a-table-column
            >
        </a-table>
    </a-card>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'deployments.edit' : 'deployments.create')"
        :confirm-loading="saving"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('deployments.application')"
                ><a-select
                    v-model:value="form.applicationId"
                    :disabled="!!editing"
                    :options="
                        applications
                            .filter(application => !application.spec.disabled)
                            .map(application => ({
                                label: application.spec.name,
                                value: application.id,
                            }))
                    "
            /></a-form-item>
            <a-form-item :label="t('deployments.node')"
                ><a-select
                    v-model:value="form.nodeId"
                    :disabled="!!editing"
                    :options="
                        nodes
                            .filter(node => !node.disabled)
                            .map(node => ({ label: node.public_host || node.id, value: node.id }))
                    "
            /></a-form-item>
            <a-form-item v-if="selectedKind === 'game_hook'" :label="t('deployments.installRoot')"
                ><a-input v-model:value="form.installRoot" placeholder="D:\Games\Application"
            /></a-form-item>
            <a-form-item v-if="selectedKind !== 'rdp'" :label="t('deployments.gpuKey')"
                ><a-input v-model:value="form.gpuKey"
            /></a-form-item>
            <template v-if="selectedKind !== 'rdp'">
                <a-form-item :label="t('deployments.gpuMemoryMiB')"
                    ><a-input-number v-model:value="form.gpuMemoryMiB" :min="1" :precision="0"
                /></a-form-item>
                <a-form-item :label="t('deployments.gpuMemoryReserveMiB')"
                    ><a-input-number
                        v-model:value="form.gpuMemoryReserveMiB"
                        :min="0"
                        :precision="0"
                /></a-form-item>
                <a-form-item :label="t('deployments.gpuComputePercent')"
                    ><a-input-number
                        v-model:value="form.gpuComputePercent"
                        :min="0.1"
                        :max="100"
                        :step="0.1"
                /></a-form-item>
                <a-form-item :label="t('deployments.gpuComputeLimitPercent')"
                    ><a-input-number
                        v-model:value="form.gpuComputeLimitPercent"
                        :min="form.gpuComputePercent"
                        :max="100"
                        :step="0.1"
                /></a-form-item>
                <a-form-item :label="t('deployments.gpuEncoderPercent')"
                    ><a-input-number
                        v-model:value="form.gpuEncoderPercent"
                        :min="0.1"
                        :max="100"
                        :step="0.1"
                /></a-form-item>
                <a-form-item :label="t('deployments.gpuEncoderLimitPercent')"
                    ><a-input-number
                        v-model:value="form.gpuEncoderLimitPercent"
                        :min="form.gpuEncoderPercent"
                        :max="100"
                        :step="0.1"
                /></a-form-item>
            </template>
            <a-form-item :label="t('deployments.capacity')"
                ><a-input-number
                    v-model:value="form.capacity"
                    :disabled="selectedKind === 'rdp'"
                    :min="1"
                    :max="64"
            /></a-form-item>
            <a-form-item v-if="editing" :label="t('deployments.disabled')"
                ><a-switch v-model:checked="form.disabled"
            /></a-form-item>
        </a-form>
    </a-modal>
</template>
