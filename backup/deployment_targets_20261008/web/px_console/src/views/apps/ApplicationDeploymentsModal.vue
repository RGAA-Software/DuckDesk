<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { message } from "ant-design-vue";
import { isAxiosError } from "axios";
import { useI18n } from "vue-i18n";
import { useManagementRefresh } from "@/model/management_events.ts";
import type { ManagedApplication } from "@/model/managed_application_api";
import { listManagedNodes, type ManagedNode } from "@/model/managed_node_api";
import {
    configureManagedDeployment,
    createManagedDeployment,
    listManagedDeployments,
    type DeploymentConfiguration,
    type ManagedDeployment,
} from "@/model/managed_deployment_api";

const props = defineProps<{ application: ManagedApplication }>();
const emit = defineEmits<{ close: [] }>();
const { t } = useI18n();
const deployments = ref<ManagedDeployment[]>([]);
const nodes = ref<ManagedNode[]>([]);
const loading = ref(false);
const loadFailed = ref(false);
const saving = ref(false);
const saveError = ref<
    | "deployments.saveFailed"
    | "deployments.sessionExpired"
    | "deployments.forbidden"
    | "deployments.invalidConfiguration"
    | "deployments.configurationConflict"
>();
const editorOpen = ref(false);
const editing = ref<ManagedDeployment>();
const form = reactive({ nodeId: "", gpuKey: "", capacity: 1, disabled: false });
const selectedKind = computed(() => props.application.spec.launch.kind);
const availableNodes = computed(() =>
    nodes.value.filter(node => !node.disabled || node.id === editing.value?.node_id),
);
const newDeploymentNodes = computed(() =>
    nodes.value.filter(
        node =>
            !node.disabled && !deployments.value.some(deployment => deployment.node_id === node.id),
    ),
);
const duplicateDeployment = computed(() =>
    editing.value
        ? undefined
        : deployments.value.find(deployment => deployment.node_id === form.nodeId),
);
const nodeOptions = computed(() =>
    availableNodes.value.map(node => {
        const alreadyDeployed =
            !editing.value && deployments.value.some(deployment => deployment.node_id === node.id);
        return {
            label: alreadyDeployed
                ? t("deployments.deployedNode", { node: node.public_host || node.id })
                : node.public_host || node.id,
            value: node.id,
            disabled: alreadyDeployed,
        };
    }),
);
let disposed = false;
let refreshId = 0;

async function refresh() {
    const requestId = ++refreshId;
    loading.value = true;
    try {
        const [deploymentRows, nodeRows] = await Promise.all([
            listManagedDeployments(),
            listManagedNodes(),
        ]);
        if (disposed || requestId !== refreshId) return;
        deployments.value = deploymentRows.filter(
            deployment => deployment.application_id === props.application.id,
        );
        nodes.value = nodeRows;
        loadFailed.value = false;
    } catch {
        if (!disposed && requestId === refreshId) loadFailed.value = true;
    } finally {
        if (!disposed && requestId === refreshId) loading.value = false;
    }
}

function create() {
    if (!newDeploymentNodes.value.length) return;
    editing.value = undefined;
    Object.assign(form, {
        nodeId: newDeploymentNodes.value[0]?.id || "",
        gpuKey: "",
        capacity: 1,
        disabled: false,
    });
    saveError.value = undefined;
    editorOpen.value = true;
}

function edit(deployment: ManagedDeployment) {
    if (deployment.application_id !== props.application.id) return;
    editing.value = deployment;
    Object.assign(form, {
        nodeId: deployment.node_id,
        gpuKey: deployment.gpu_key || "",
        capacity: deployment.capacity,
        disabled: deployment.disabled,
    });
    saveError.value = undefined;
    editorOpen.value = true;
}

function configuration(): DeploymentConfiguration {
    const kind = selectedKind.value;
    return {
        target: { kind },
        gpu_key: kind === "rdp" ? null : form.gpuKey.trim() || null,
        capacity: kind === "rdp" ? 1 : form.capacity,
        disabled: form.disabled,
    };
}

async function save() {
    if (saving.value || duplicateDeployment.value) return;
    if (!form.nodeId) {
        message.error(t("deployments.validation.selection"));
        return;
    }
    saving.value = true;
    saveError.value = undefined;
    try {
        if (editing.value) {
            await configureManagedDeployment(editing.value, configuration());
        } else {
            await createManagedDeployment(props.application.id, form.nodeId, configuration());
        }
        if (disposed) return;
        editorOpen.value = false;
        message.success(t("deployments.messages.saved"));
        await refresh();
    } catch (error) {
        if (disposed) return;
        const status = isAxiosError(error) ? error.response?.status : undefined;
        if (status === 409 && !editing.value) {
            // Another administrator may have created this application/node deployment while the editor was open.
            await refresh();
            if (disposed || duplicateDeployment.value) return;
        }
        saveError.value =
            status === 401
                ? "deployments.sessionExpired"
                : status === 403
                  ? "deployments.forbidden"
                  : status === 400
                    ? "deployments.invalidConfiguration"
                    : status === 409
                      ? "deployments.configurationConflict"
                      : "deployments.saveFailed";
    } finally {
        if (!disposed) saving.value = false;
    }
}

function close() {
    if (!saving.value) emit("close");
}
function nodeName(deployment: ManagedDeployment) {
    return (
        nodes.value.find(node => node.id === deployment.node_id)?.public_host || deployment.node_id
    );
}
onMounted(refresh);
useManagementRefresh(["deployments", "nodes"], refresh);
onBeforeUnmount(() => {
    disposed = true;
    ++refreshId;
});
</script>

<template>
    <a-modal
        :open="true"
        :title="t('deployments.dialogTitle', { name: application.spec.name })"
        width="min(1100px, 96vw)"
        :mask-closable="false"
        :keyboard="false"
        :closable="!saving"
        @cancel="close"
    >
        <template #footer>
            <a-button v-if="editorOpen" :disabled="saving" @click="editorOpen = false">{{
                t("deployments.back")
            }}</a-button>
            <a-button
                v-if="editorOpen"
                type="primary"
                :loading="saving"
                :disabled="!!duplicateDeployment || !form.nodeId"
                @click="save"
                >{{ t("deployments.save") }}</a-button
            >
            <a-button v-else @click="close">{{ t("deployments.close") }}</a-button>
        </template>
        <a-alert
            v-if="loadFailed"
            type="error"
            show-icon
            :message="t('deployments.loadFailed')"
            style="margin-bottom: 12px"
        >
            <template #action
                ><a-button size="small" @click="refresh">{{
                    t("deployments.retry")
                }}</a-button></template
            >
        </a-alert>
        <template v-if="editorOpen">
            <a-alert
                v-if="duplicateDeployment"
                type="warning"
                show-icon
                :message="t('deployments.duplicate')"
                style="margin-bottom: 12px"
            >
                <template #action
                    ><a-button :disabled="saving" @click="edit(duplicateDeployment)">{{
                        t("deployments.editExisting")
                    }}</a-button></template
                >
            </a-alert>
            <a-alert
                v-else-if="saveError"
                type="error"
                show-icon
                :message="t(saveError)"
                style="margin-bottom: 12px"
            />
            <a-form layout="vertical" :disabled="saving">
                <a-form-item :label="t('deployments.application')"
                    ><a-input :value="application.spec.name" disabled
                /></a-form-item>
                <a-form-item
                    :label="t('deployments.node')"
                    :extra="editing ? t('deployments.fixedNodeHelp') : undefined"
                >
                    <a-select
                        v-model:value="form.nodeId"
                        :disabled="!!editing"
                        :options="nodeOptions"
                    />
                </a-form-item>
                <a-form-item v-if="selectedKind !== 'rdp'" :label="t('deployments.gpuKey')"
                    ><a-input v-model:value="form.gpuKey"
                /></a-form-item>
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
        </template>
        <template v-else>
            <a-space style="margin-bottom: 12px">
                <a-button
                    type="primary"
                    :disabled="
                        loading ||
                        loadFailed ||
                        !newDeploymentNodes.length ||
                        application.spec.disabled
                    "
                    @click="create"
                    >{{ t("deployments.create") }}</a-button
                >
            </a-space>
            <a-alert
                v-if="!loading && !loadFailed && !newDeploymentNodes.length"
                type="info"
                show-icon
                :message="
                    t(
                        nodes.some(node => !node.disabled)
                            ? 'deployments.allNodesDeployed'
                            : 'deployments.noNodes',
                    )
                "
                style="margin-bottom: 12px"
            />
            <a-alert
                type="info"
                show-icon
                :message="t('deployments.statusNotice')"
                style="margin-bottom: 12px"
            />
            <a-table
                :data-source="deployments"
                row-key="id"
                :loading="loading"
                :pagination="false"
                :locale="{ emptyText: t('deployments.empty') }"
            >
                <a-table-column :title="t('deployments.node')"
                    ><template #default="{ record }">{{
                        nodeName(record)
                    }}</template></a-table-column
                >
                <a-table-column :title="t('applications.kind')"
                    ><template #default="{ record }">{{
                        t(`applications.kinds.${record.kind}`)
                    }}</template></a-table-column
                >
                <a-table-column :title="t('deployments.capacity')" data-index="capacity" />
                <a-table-column :title="t('deployments.observed')"
                    ><template #default="{ record }">
                        <a-tag
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
                        >
                    </template></a-table-column
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
        </template>
    </a-modal>
</template>
