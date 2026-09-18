<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { Modal, message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { copyText } from "@/util/clipboard";
import { listManagedDevices, type ManagedDevice } from "@/model/managed_device_api";
import {
    configureManagedNode,
    createManagedNode,
    deleteManagedNode,
    listManagedNodes,
    rotateManagedNodeCredential,
    type ManagedNode,
    type NodeProduct,
} from "@/model/managed_node_api";

const { t } = useI18n();
const nodes = ref<ManagedNode[]>([]);
const devices = ref<ManagedDevice[]>([]);
const loading = ref(false);
const saving = ref(false);
const editorOpen = ref(false);
const editing = ref<ManagedNode>();
const form = reactive({
    deviceId: "",
    product: "cloud_node" as NodeProduct,
    maxInstances: 4,
    draining: false,
    disabled: false,
});
const credentialOpen = ref(false);
const nodeToken = ref("");

const availableDevices = computed(() => {
    const assigned = new Set(nodes.value.map(node => node.device_id));
    return devices.value.filter(
        device =>
            !device.disabled &&
            (!assigned.has(device.id) || device.id === editing.value?.device_id),
    );
});

async function refresh() {
    loading.value = true;
    try {
        [nodes.value, devices.value] = await Promise.all([
            listManagedNodes(),
            listManagedDevices(),
        ]);
    } finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, {
        deviceId: availableDevices.value[0]?.id || "",
        product: "cloud_node",
        maxInstances: 4,
        draining: false,
        disabled: false,
    });
    editorOpen.value = true;
}

function edit(node: ManagedNode) {
    editing.value = node;
    Object.assign(form, {
        deviceId: node.device_id,
        product: node.product,
        maxInstances: node.max_instances,
        draining: node.draining,
        disabled: node.disabled,
    });
    editorOpen.value = true;
}

function showCredential(token: string) {
    nodeToken.value = token;
    credentialOpen.value = true;
}

async function save() {
    if (!editing.value && !form.deviceId) {
        message.error(t("nodes.validation.device"));
        return;
    }
    saving.value = true;
    try {
        if (editing.value) {
            await configureManagedNode(editing.value, {
                draining: form.draining,
                disabled: form.disabled,
                max_instances: form.maxInstances,
            });
        } else {
            const result = await createManagedNode(form.deviceId, form.product, form.maxInstances);
            showCredential(result.node_token);
        }
        editorOpen.value = false;
        await refresh();
    } finally {
        saving.value = false;
    }
}

function rotateCredential(node: ManagedNode) {
    Modal.confirm({
        title: t("nodes.confirm.rotateTitle"),
        content: t("nodes.confirm.rotateImpact"),
        okType: "danger",
        async onOk() {
            const result = await rotateManagedNodeCredential(node);
            showCredential(result.node_token);
            await refresh();
        },
    });
}

function remove(node: ManagedNode) {
    Modal.confirm({
        title: t("nodes.confirm.deleteTitle"),
        content: t("nodes.confirm.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteManagedNode(node);
            await refresh();
        },
    });
}

async function copyCredential() {
    await copyText(nodeToken.value);
    message.success(t("nodes.messages.copied"));
}

function deviceName(node: ManagedNode) {
    return devices.value.find(device => device.id === node.device_id)?.name || node.device_id;
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('nodes.title')">
        <template #extra
            ><a-button type="primary" :disabled="availableDevices.length === 0" @click="create">{{
                t("nodes.create")
            }}</a-button></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('nodes.statusNotice')"
            style="margin-bottom: 12px"
        />
        <a-table :data-source="nodes" row-key="id" :loading="loading" :pagination="false">
            <a-table-column :title="t('nodes.device')"
                ><template #default="{ record }">{{ deviceName(record) }}</template></a-table-column
            >
            <a-table-column :title="t('nodes.product')"
                ><template #default="{ record }">{{
                    t(`nodes.products.${record.product}`)
                }}</template></a-table-column
            >
            <a-table-column :title="t('nodes.state')"
                ><template #default="{ record }"
                    ><a-tag :color="record.fresh && !record.disabled ? 'green' : 'default'"
                        >{{ record.state }} /
                        {{ record.fresh ? t("nodes.fresh") : t("nodes.stale") }}</a-tag
                    ></template
                ></a-table-column
            >
            <a-table-column :title="t('nodes.capacity')" data-index="max_instances" />
            <a-table-column :title="t('nodes.endpoint')"
                ><template #default="{ record }">{{
                    record.public_host ? `${record.public_host}:${record.desktop_port || "-"}` : "-"
                }}</template></a-table-column
            >
            <a-table-column :title="t('nodes.capabilities')"
                ><template #default="{ record }"
                    ><a-space
                        ><a-tag v-if="record.game_hook">Game Hook</a-tag
                        ><a-tag v-if="record.webview">WebView</a-tag
                        ><a-tag v-if="record.rdp">RDP</a-tag></a-space
                    ></template
                ></a-table-column
            >
            <a-table-column :title="t('identity.users.actions')" width="360"
                ><template #default="{ record }"
                    ><a-space wrap
                        ><a-button size="small" @click="edit(record)">{{
                            t("identity.actions.edit")
                        }}</a-button
                        ><a-button size="small" danger @click="rotateCredential(record)">{{
                            t("nodes.rotate")
                        }}</a-button
                        ><a-button size="small" danger @click="remove(record)">{{
                            t("identity.actions.delete")
                        }}</a-button></a-space
                    ></template
                ></a-table-column
            >
        </a-table>
    </a-card>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'nodes.edit' : 'nodes.create')"
        :confirm-loading="saving"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('nodes.device')"
                ><a-select
                    v-model:value="form.deviceId"
                    :disabled="!!editing"
                    :options="
                        availableDevices.map(device => ({
                            label: `${device.name} (${device.public_code})`,
                            value: device.id,
                        }))
                    "
            /></a-form-item>
            <a-form-item :label="t('nodes.product')"
                ><a-select
                    v-model:value="form.product"
                    :disabled="!!editing"
                    :options="
                        (['cloud_node', 'remote'] as const).map(product => ({
                            value: product,
                            label: t(`nodes.products.${product}`),
                        }))
                    "
            /></a-form-item>
            <a-form-item :label="t('nodes.capacity')"
                ><a-input-number v-model:value="form.maxInstances" :min="1" :max="64"
            /></a-form-item>
            <a-form-item v-if="editing" :label="t('nodes.draining')"
                ><a-switch v-model:checked="form.draining"
            /></a-form-item>
            <a-form-item v-if="editing" :label="t('nodes.disabled')"
                ><a-switch v-model:checked="form.disabled"
            /></a-form-item>
        </a-form>
    </a-modal>

    <a-modal v-model:open="credentialOpen" :title="t('nodes.credentialTitle')" :footer="null">
        <a-alert type="warning" show-icon :message="t('nodes.credentialNotice')" />
        <a-typography-paragraph copyable style="margin-top: 16px; word-break: break-all">{{
            nodeToken
        }}</a-typography-paragraph>
        <a-button type="primary" @click="copyCredential">{{ t("nodes.copyCredential") }}</a-button>
    </a-modal>
</template>
