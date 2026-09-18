<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { Modal, message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { copyText } from "@/util/clipboard";
import {
    listAllAdminUsers,
    listGroups,
    type GroupView,
    type UserAdminView,
} from "@/model/identity_api";
import {
    createManagedDevice,
    deleteManagedDevice,
    getManagedDeviceAccess,
    listManagedDevices,
    replaceManagedDeviceAccess,
    rotateManagedDeviceCredential,
    updateManagedDevice,
    type DevicePlatform,
    type ManagedDevice,
} from "@/model/managed_device_api";

const { t } = useI18n();
const devices = ref<ManagedDevice[]>([]);
const users = ref<UserAdminView[]>([]);
const groups = ref<GroupView[]>([]);
const loading = ref(false);
const keyword = ref("");
const editorOpen = ref(false);
const editorSaving = ref(false);
const editing = ref<ManagedDevice>();
const form = reactive({ name: "", platform: "windows" as DevicePlatform, disabled: false });
const accessOpen = ref(false);
const accessSaving = ref(false);
const accessDevice = ref<ManagedDevice>();
const accessForm = reactive({ users: [] as string[], groups: [] as string[] });
const credentialOpen = ref(false);
const enrollmentToken = ref("");

const visibleDevices = computed(() => {
    const normalizedKeyword = keyword.value.trim().toLocaleLowerCase();
    if (!normalizedKeyword) return devices.value;
    return devices.value.filter(device =>
        [device.name, device.public_code, device.id, device.platform].some(value =>
            value.toLocaleLowerCase().includes(normalizedKeyword),
        ),
    );
});

const platformOptions = computed(() =>
    (["windows", "linux", "macos", "android"] as DevicePlatform[]).map(platform => ({
        value: platform,
        label: t(`devices.platforms.${platform}`),
    })),
);

async function refresh() {
    loading.value = true;
    try {
        const [deviceList, userList, groupList] = await Promise.all([
            listManagedDevices(),
            listAllAdminUsers(),
            listGroups(),
        ]);
        devices.value = deviceList;
        users.value = userList.filter(user => !user.disabled && !user.deleted_at);
        groups.value = groupList;
    } finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, { name: "", platform: "windows", disabled: false });
    editorOpen.value = true;
}

function edit(device: ManagedDevice) {
    editing.value = device;
    Object.assign(form, {
        name: device.name,
        platform: device.platform,
        disabled: device.disabled,
    });
    editorOpen.value = true;
}

function showCredential(token: string) {
    enrollmentToken.value = token;
    credentialOpen.value = true;
}

async function save() {
    const name = form.name.trim();
    if (!name || name !== form.name) {
        message.error(t("devices.validation.name"));
        return;
    }
    editorSaving.value = true;
    try {
        if (editing.value) {
            await updateManagedDevice(editing.value, name, form.disabled);
        } else {
            const result = await createManagedDevice(name, form.platform);
            showCredential(result.enrollment_token);
        }
        editorOpen.value = false;
        await refresh();
    } finally {
        editorSaving.value = false;
    }
}

async function openAccess(device: ManagedDevice) {
    const access = await getManagedDeviceAccess(device);
    accessDevice.value = device;
    Object.assign(accessForm, access);
    accessOpen.value = true;
}

async function saveAccess() {
    if (!accessDevice.value) return;
    accessSaving.value = true;
    try {
        await replaceManagedDeviceAccess(accessDevice.value, accessForm);
        accessOpen.value = false;
        message.success(t("devices.messages.accessSaved"));
        await refresh();
    } finally {
        accessSaving.value = false;
    }
}

function rotateCredential(device: ManagedDevice) {
    Modal.confirm({
        title: t("devices.confirm.rotateTitle"),
        content: t("devices.confirm.rotateImpact"),
        okType: "danger",
        async onOk() {
            const result = await rotateManagedDeviceCredential(device);
            showCredential(result.enrollment_token);
            await refresh();
        },
    });
}

function remove(device: ManagedDevice) {
    Modal.confirm({
        title: t("devices.confirm.deleteTitle", { name: device.name }),
        content: t("devices.confirm.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteManagedDevice(device);
            await refresh();
        },
    });
}

async function copyCredential() {
    await copyText(enrollmentToken.value);
    message.success(t("devices.messages.copied"));
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('devices.title')">
        <template #extra>
            <a-space>
                <a-input-search v-model:value="keyword" :placeholder="t('devices.search')" />
                <a-button type="primary" @click="create">{{ t("devices.create") }}</a-button>
            </a-space>
        </template>
        <a-alert
            type="info"
            show-icon
            :message="t('devices.inventoryNotice')"
            style="margin-bottom: 12px"
        />
        <a-table
            :data-source="visibleDevices"
            row-key="id"
            :loading="loading"
            :pagination="{ pageSize: 20 }"
        >
            <a-table-column :title="t('devices.name')" data-index="name" />
            <a-table-column :title="t('devices.publicCode')" data-index="public_code" />
            <a-table-column :title="t('devices.platform')">
                <template #default="{ record }">{{
                    t(`devices.platforms.${record.platform}`)
                }}</template>
            </a-table-column>
            <a-table-column :title="t('devices.registeredAt')">
                <template #default="{ record }">{{
                    new Date(record.registered_at).toLocaleString()
                }}</template>
            </a-table-column>
            <a-table-column :title="t('identity.users.status')">
                <template #default="{ record }">
                    <a-tag :color="record.disabled ? 'red' : 'green'">
                        {{
                            t(
                                record.disabled
                                    ? "identity.states.disabled"
                                    : "identity.states.active",
                            )
                        }}
                    </a-tag>
                </template>
            </a-table-column>
            <a-table-column :title="t('identity.users.actions')" width="440">
                <template #default="{ record }">
                    <a-space wrap>
                        <a-button size="small" @click="edit(record)">{{
                            t("identity.actions.edit")
                        }}</a-button>
                        <a-button size="small" @click="openAccess(record)">{{
                            t("devices.access")
                        }}</a-button>
                        <a-button size="small" danger @click="rotateCredential(record)">{{
                            t("devices.rotate")
                        }}</a-button>
                        <a-button size="small" danger @click="remove(record)">{{
                            t("identity.actions.delete")
                        }}</a-button>
                    </a-space>
                </template>
            </a-table-column>
        </a-table>
    </a-card>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'devices.edit' : 'devices.create')"
        :confirm-loading="editorSaving"
        :ok-text="t('identity.actions.save')"
        :cancel-text="t('identity.actions.cancel')"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('devices.name')">
                <a-input v-model:value="form.name" :maxlength="128" />
            </a-form-item>
            <a-form-item :label="t('devices.platform')">
                <a-select
                    v-model:value="form.platform"
                    :disabled="!!editing"
                    :options="platformOptions"
                />
            </a-form-item>
            <a-form-item v-if="editing" :label="t('devices.disabled')">
                <a-switch v-model:checked="form.disabled" />
            </a-form-item>
        </a-form>
    </a-modal>

    <a-modal
        v-model:open="accessOpen"
        :title="t('devices.accessTitle', { name: accessDevice?.name || '' })"
        :confirm-loading="accessSaving"
        :ok-text="t('identity.actions.save')"
        :cancel-text="t('identity.actions.cancel')"
        @ok="saveAccess"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('devices.users')">
                <a-select
                    v-model:value="accessForm.users"
                    mode="multiple"
                    :options="users.map(user => ({ label: user.username, value: user.uid }))"
                />
            </a-form-item>
            <a-form-item :label="t('devices.groups')">
                <a-select
                    v-model:value="accessForm.groups"
                    mode="multiple"
                    :options="groups.map(group => ({ label: group.name, value: group.gid }))"
                />
            </a-form-item>
        </a-form>
    </a-modal>

    <a-modal v-model:open="credentialOpen" :title="t('devices.credentialTitle')" :footer="null">
        <a-alert type="warning" show-icon :message="t('devices.credentialNotice')" />
        <a-typography-paragraph copyable style="margin-top: 16px; word-break: break-all">
            {{ enrollmentToken }}
        </a-typography-paragraph>
        <a-button type="primary" @click="copyCredential">{{
            t("devices.copyCredential")
        }}</a-button>
    </a-modal>
</template>
