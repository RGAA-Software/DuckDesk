<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { message } from "ant-design-vue";
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
const saveFailed = ref(false);
const editorOpen = ref(false);
const editing = ref<ManagedDeployment>();
const form = reactive({ nodeId: "", gpuKey: "", capacity: 1, disabled: false });
const selectedKind = computed(() => props.application.spec.launch.kind);
const availableNodes = computed(() =>
    nodes.value.filter(node => !node.disabled || node.id === editing.value?.node_id),
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
    editing.value = undefined;
    Object.assign(form, {
        nodeId: nodes.value.find(node => !node.disabled)?.id || "",
        gpuKey: "",
        capacity: 1,
        disabled: false,
    });
    saveFailed.value = false;
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
    saveFailed.value = false;
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
    if (saving.value) return;
    if (!form.nodeId) {
        message.error(t("deployments.validation.selection"));
        return;
    }
    saving.value = true;
    saveFailed.value = false;
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
    } catch {
        if (!disposed) saveFailed.value = true;
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
            <a-button v-if="editorOpen" type="primary" :loading="saving" @click="save">{{
                t("deployments.save")
            }}</a-button>
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
                v-if="saveFailed"
                type="error"
                show-icon
                :message="t('deployments.saveFailed')"
                style="margin-bottom: 12px"
            />
            <a-form layout="vertical" :disabled="saving">
                <a-form-item :label="t('deployments.application')"
                    ><a-input :value="application.spec.name" disabled
                /></a-form-item>
                <a-form-item :label="t('deployments.node')">
                    <a-select
                        v-model:value="form.nodeId"
                        :disabled="!!editing"
                        :options="
                            availableNodes.map(node => ({
                                label: node.public_host || node.id,
                                value: node.id,
                            }))
                        "
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
                        loading || loadFailed || !availableNodes.length || application.spec.disabled
                    "
                    @click="create"
                    >{{ t("deployments.create") }}</a-button
                >
            </a-space>
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
