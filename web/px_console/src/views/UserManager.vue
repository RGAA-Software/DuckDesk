<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { Modal, message, type FormInstance } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import {
    blockGuestSession,
    createAdminUser,
    deleteAdminUser,
    listAdminUsers,
    listGuestSessions,
    patchAdminUser,
    resetAdminUserPassword,
    type GuestSessionView,
    type ManagedRole,
    type UserAdminView,
} from "@/model/identity_api";

const { t } = useI18n();
const users = ref<UserAdminView[]>([]);
const total = ref(0);
const page = ref(1);
const keyword = ref("");
const loading = ref(false);
const editorOpen = ref(false);
const editorFormRef = ref<FormInstance>();
const editorSaving = ref(false);
const editing = ref<UserAdminView>();
const form = reactive({ username: "", password: "", role: "user" as ManagedRole, disabled: false });
const resetOpen = ref(false);
const resetSaving = ref(false);
const resetUser = ref<UserAdminView>();
const resetForm = reactive({ password: "" });
const guestOpen = ref(false);
const guestLoading = ref(false);
const guests = ref<GuestSessionView[]>([]);

const roleOptions = computed<Array<{ label: string; value: ManagedRole }>>(() => [
    { label: t("identity.roles.user"), value: "user" },
    { label: t("identity.roles.admin"), value: "admin" },
    { label: t("identity.roles.viewer"), value: "viewer" },
]);

function usernameError(value: string): string | undefined {
    const length = [...value].length;
    if (length < 2 || length > 64) return t("identity.validation.usernameLength");
    if (value.trim() !== value) return t("identity.validation.usernameWhitespace");
    if (value.includes("/") || value.includes("\\")) return t("identity.validation.usernameSlash");
    if (/[\u0000-\u001f\u007f-\u009f]/u.test(value))
        return t("identity.validation.usernameControl");
    return undefined;
}

function passwordError(value: string): string | undefined {
    if (!value) return t("identity.validation.passwordRequired");
    const length = [...value].length;
    if (length < 8 || length > 128) return t("identity.validation.passwordLength");
    if (!value.trim()) return t("identity.validation.passwordWhitespace");
    return undefined;
}

const editorRules = {
    username: [
        {
            validator: (_rule: unknown, value: string) => {
                const error = usernameError(value || "");
                return error ? Promise.reject(new Error(error)) : Promise.resolve();
            },
            trigger: ["blur", "change"],
        },
    ],
    password: [
        {
            validator: (_rule: unknown, value: string) => {
                if (editing.value) return Promise.resolve();
                const error = passwordError(value || "");
                return error ? Promise.reject(new Error(error)) : Promise.resolve();
            },
            trigger: ["blur", "change"],
        },
    ],
};

async function refresh() {
    loading.value = true;
    try {
        const result = await listAdminUsers(page.value, keyword.value);
        users.value = result.items;
        total.value = result.total;
    } finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, { username: "", password: "", role: "user", disabled: false });
    editorFormRef.value?.clearValidate();
    editorOpen.value = true;
}

function edit(user: UserAdminView) {
    editing.value = user;
    Object.assign(form, {
        username: user.username,
        password: "",
        role: user.role,
        disabled: user.disabled,
    });
    editorFormRef.value?.clearValidate();
    editorOpen.value = true;
}

function requestError(error: unknown, fallbackKey: string): string {
    const response = (error as { response?: { data?: { message?: string } } })?.response;
    return response?.data?.message || (error instanceof Error ? error.message : t(fallbackKey));
}

async function save() {
    try {
        await editorFormRef.value?.validate();
    } catch {
        return;
    }
    editorSaving.value = true;
    try {
        if (editing.value) {
            await patchAdminUser(editing.value, { role: form.role, disabled: form.disabled });
        } else {
            await createAdminUser({
                username: form.username,
                password: form.password,
                role: form.role,
            });
        }
        editorOpen.value = false;
        message.success(t("identity.users.saved"));
        await refresh();
    } catch (error) {
        message.error(requestError(error, "identity.users.saveFailed"));
    } finally {
        editorSaving.value = false;
    }
}

async function toggle(user: UserAdminView) {
    try {
        await patchAdminUser(user, { role: user.role, disabled: !user.disabled });
        await refresh();
    } catch (error) {
        message.error(requestError(error, "identity.users.saveFailed"));
    }
}

function openPasswordReset(user: UserAdminView) {
    resetUser.value = user;
    resetForm.password = "";
    resetOpen.value = true;
}

async function resetPassword() {
    const user = resetUser.value;
    const error = passwordError(resetForm.password);
    if (!user || error) {
        message.error(error || t("identity.users.resetFailed"));
        return;
    }
    resetSaving.value = true;
    try {
        await resetAdminUserPassword(user, resetForm.password);
        resetOpen.value = false;
        message.success(t("identity.users.resetSucceeded"));
        await refresh();
    } catch (requestFailure) {
        message.error(requestError(requestFailure, "identity.users.resetFailed"));
    } finally {
        resetSaving.value = false;
    }
}

async function showGuests() {
    guestOpen.value = true;
    guestLoading.value = true;
    try {
        guests.value = await listGuestSessions();
    } finally {
        guestLoading.value = false;
    }
}

function blockGuest(guest: GuestSessionView, includeSource: boolean) {
    Modal.confirm({
        title: t(includeSource ? "identity.guests.confirmSource" : "identity.guests.confirmGuest"),
        content: t(includeSource ? "identity.guests.sourceImpact" : "identity.guests.guestImpact"),
        okType: "danger",
        async onOk() {
            await blockGuestSession(guest, includeSource);
            message.success(t("identity.guests.blocked"));
            guests.value = await listGuestSessions();
        },
    });
}

function remove(user: UserAdminView) {
    Modal.confirm({
        title: t("identity.users.deleteTitle", { username: user.username }),
        content: t("identity.users.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteAdminUser(user);
            await refresh();
        },
    });
}

function onTableChange(pagination: { current?: number }) {
    page.value = pagination.current || 1;
    void refresh();
}

function formatTime(value?: string | null) {
    return value ? new Date(value).toLocaleString() : "-";
}

function guestState(guest: GuestSessionView) {
    if (guest.blocked) return t("identity.states.blocked");
    if (guest.revoked_at) return t("identity.states.revoked");
    if (new Date(guest.expires_at).getTime() <= Date.now()) return t("identity.states.expired");
    return t("identity.states.active");
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('identity.users.title')">
        <template #extra>
            <a-space>
                <a-input-search
                    v-model:value="keyword"
                    :placeholder="t('identity.users.search')"
                    @search="refresh"
                />
                <a-button @click="showGuests">{{ t("identity.guests.title") }}</a-button>
                <a-button type="primary" @click="create">{{ t("identity.users.create") }}</a-button>
            </a-space>
        </template>
        <a-table
            :data-source="users"
            row-key="uid"
            :loading="loading"
            :pagination="{ current: page, total, pageSize: 20 }"
            @change="onTableChange"
        >
            <a-table-column :title="t('identity.users.username')" data-index="username" />
            <a-table-column :title="t('identity.users.role')">
                <template #default="{ record }">{{ t(`identity.roles.${record.role}`) }}</template>
            </a-table-column>
            <a-table-column :title="t('identity.users.createdAt')">
                <template #default="{ record }">{{ formatTime(record.created_at) }}</template>
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
            <a-table-column :title="t('identity.users.actions')" width="390">
                <template #default="{ record }">
                    <a-space wrap>
                        <a-button size="small" @click="edit(record)">{{
                            t("identity.actions.edit")
                        }}</a-button>
                        <a-button size="small" @click="toggle(record)">
                            {{
                                t(
                                    record.disabled
                                        ? "identity.actions.enable"
                                        : "identity.actions.disable",
                                )
                            }}
                        </a-button>
                        <a-button
                            size="small"
                            :disabled="record.disabled"
                            @click="openPasswordReset(record)"
                        >
                            {{ t("identity.actions.resetPassword") }}
                        </a-button>
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
        :title="t(editing ? 'identity.users.edit' : 'identity.users.create')"
        :confirm-loading="editorSaving"
        :ok-text="t('identity.actions.save')"
        :cancel-text="t('identity.actions.cancel')"
        @ok="save"
    >
        <a-form ref="editorFormRef" :model="form" :rules="editorRules" layout="vertical">
            <a-form-item :label="t('identity.users.username')" name="username">
                <a-input v-model:value="form.username" :disabled="!!editing" :maxlength="64" />
            </a-form-item>
            <a-form-item v-if="!editing" :label="t('identity.users.password')" name="password">
                <a-input-password v-model:value="form.password" :maxlength="128" />
            </a-form-item>
            <a-form-item :label="t('identity.users.role')">
                <a-select v-model:value="form.role" :options="roleOptions" />
            </a-form-item>
            <a-form-item v-if="editing" :label="t('identity.users.disabled')">
                <a-switch v-model:checked="form.disabled" />
            </a-form-item>
        </a-form>
    </a-modal>

    <a-modal
        v-model:open="resetOpen"
        :title="t('identity.users.resetTitle', { username: resetUser?.username || '' })"
        :confirm-loading="resetSaving"
        :ok-text="t('identity.actions.save')"
        :cancel-text="t('identity.actions.cancel')"
        @ok="resetPassword"
    >
        <a-alert type="info" show-icon :message="t('identity.users.passwordNotStored')" />
        <a-form layout="vertical" style="margin-top: 16px">
            <a-form-item :label="t('identity.users.newPassword')">
                <a-input-password v-model:value="resetForm.password" :maxlength="128" />
            </a-form-item>
        </a-form>
    </a-modal>

    <a-modal
        v-model:open="guestOpen"
        :title="t('identity.guests.title')"
        :footer="null"
        width="1000px"
    >
        <a-alert
            type="warning"
            show-icon
            :message="t('identity.guests.privacyNotice')"
            style="margin-bottom: 12px"
        />
        <a-table
            :data-source="guests"
            row-key="id"
            :loading="guestLoading"
            :pagination="{ pageSize: 20 }"
            size="small"
            :scroll="{ x: 900 }"
        >
            <a-table-column :title="t('identity.guests.identity')" data-index="id" width="310" />
            <a-table-column
                :title="t('identity.guests.client')"
                data-index="client_type"
                width="110"
            />
            <a-table-column :title="t('identity.guests.createdAt')" width="180">
                <template #default="{ record }">{{ formatTime(record.created_at) }}</template>
            </a-table-column>
            <a-table-column :title="t('identity.guests.expiresAt')" width="180">
                <template #default="{ record }">{{ formatTime(record.expires_at) }}</template>
            </a-table-column>
            <a-table-column :title="t('identity.users.status')" width="90">
                <template #default="{ record }">{{ guestState(record) }}</template>
            </a-table-column>
            <a-table-column :title="t('identity.users.actions')" width="190">
                <template #default="{ record }">
                    <a-space>
                        <a-button
                            size="small"
                            danger
                            :disabled="record.blocked"
                            @click="blockGuest(record, false)"
                        >
                            {{ t("identity.guests.blockGuest") }}
                        </a-button>
                        <a-button
                            size="small"
                            danger
                            :disabled="record.blocked"
                            @click="blockGuest(record, true)"
                        >
                            {{ t("identity.guests.blockSource") }}
                        </a-button>
                    </a-space>
                </template>
            </a-table-column>
        </a-table>
    </a-modal>
</template>
