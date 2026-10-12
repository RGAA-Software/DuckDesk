<script setup lang="ts">
import { computed, watch } from "vue";
import { useI18n } from "vue-i18n";
import type { ManagedApplication } from "@/model/managed_application_api";
import type { ManagedNode } from "@/model/managed_node_api";
import type { ManagedDeployment } from "@/model/managed_deployment_api";
import { useDeploymentTargets } from "@/composables/useDeploymentTargets";
const props = defineProps<{
    application: ManagedApplication;
    nodes: ManagedNode[];
    deployments: ManagedDeployment[];
}>();
const emit = defineEmits<{ back: []; busy: [value: boolean]; saved: [] }>();
const { t } = useI18n();
const { selectedNodeIds, newNodeCapacity, saving, results, errorKey, complete, hasFailures, save } =
    useDeploymentTargets(
        props.application.id,
        props.application.spec.launch.kind,
        props.deployments,
    );
const nodeNames = computed(
    () => new Map(props.nodes.map(node => [node.id, node.public_host || node.id])),
);
const nodeOptions = computed(() => {
    const options = props.nodes.map(node => ({
        value: node.id,
        label: node.public_host || node.id,
        disabled: node.disabled && !selectedNodeIds.value.includes(node.id),
    }));
    for (const nodeId of selectedNodeIds.value)
        if (!nodeNames.value.has(nodeId))
            options.push({ value: nodeId, label: nodeId, disabled: false });
    return options;
});
watch(saving, value => emit("busy", value), { flush: "sync" });
watch(complete, value => {
    if (value) emit("saved");
});
watch([selectedNodeIds, newNodeCapacity], () => {
    if (!saving.value) {
        complete.value = false;
        results.value = [];
        errorKey.value = undefined;
    }
});
</script>

<template>
    <a-alert
        type="info"
        show-icon
        :message="t('deployments.targetsHelp')"
        style="margin-bottom: 12px"
    />
    <a-form layout="vertical" :disabled="saving">
        <a-form-item :label="t('deployments.targetMachines')">
            <a-select
                v-model:value="selectedNodeIds"
                mode="multiple"
                :options="nodeOptions"
                :placeholder="t('deployments.selectMachines')"
                option-filter-prop="label"
            />
        </a-form-item>
        <a-form-item
            :label="t('deployments.newNodeCapacity')"
            :extra="t('deployments.preserveSettings')"
        >
            <a-input-number
                v-model:value="newNodeCapacity"
                :disabled="application.spec.launch.kind === 'rdp'"
                :min="1"
                :max="64"
            />
        </a-form-item>
    </a-form>
    <a-alert
        v-if="!selectedNodeIds.length"
        type="warning"
        show-icon
        :message="t('deployments.noTargetsWarning')"
        style="margin-bottom: 12px"
    />
    <a-alert
        v-if="errorKey"
        type="error"
        show-icon
        :message="t(errorKey)"
        style="margin-bottom: 12px"
    />
    <a-alert
        v-if="complete"
        type="success"
        show-icon
        :message="t('deployments.targetsSaved')"
        style="margin-bottom: 12px"
    />
    <a-table
        v-if="results.length"
        :data-source="results"
        row-key="nodeId"
        :pagination="false"
        size="small"
    >
        <a-table-column :title="t('deployments.node')"
            ><template #default="{ record }">{{
                nodeNames.get(record.nodeId) || record.nodeId
            }}</template></a-table-column
        >
        <a-table-column :title="t('identity.users.actions')"
            ><template #default="{ record }">{{
                t(`deployments.targetActions.${record.action}`)
            }}</template></a-table-column
        >
        <a-table-column :title="t('identity.users.status')"
            ><template #default="{ record }">
                <a-tag
                    :color="
                        record.status === 'error'
                            ? 'red'
                            : record.status === 'saved' || record.status === 'unchanged'
                              ? 'green'
                              : 'default'
                    "
                    >{{ t(`deployments.targetStates.${record.status}`) }}</a-tag
                >
                <span v-if="record.errorKey">{{ t(record.errorKey) }}</span>
            </template></a-table-column
        >
    </a-table>
    <a-space style="display: flex; justify-content: flex-end; margin-top: 16px">
        <a-button :disabled="saving" @click="emit('back')">{{
            t("deployments.viewNodeSettings")
        }}</a-button>
        <a-button
            type="primary"
            :loading="saving"
            :disabled="
                complete ||
                !Number.isInteger(newNodeCapacity) ||
                newNodeCapacity < 1 ||
                newNodeCapacity > 64
            "
            @click="save"
            >{{ t(hasFailures ? "deployments.retryTargets" : "deployments.saveTargets") }}</a-button
        >
    </a-space>
</template>
