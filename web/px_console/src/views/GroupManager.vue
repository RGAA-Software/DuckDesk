<script setup lang="ts">
import { onMounted, reactive, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import { Modal, message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import {
    createGroup,
    deleteGroup,
    groupIds,
    listAllAdminUsers,
    listGroups,
    replaceGroupIds,
    type GroupView,
    type UserAdminView,
} from "@/model/identity_api";

const { t } = useI18n();
const groups = ref<GroupView[]>([]);
const users = ref<UserAdminView[]>([]);
const loading = ref(false);
const saving = ref(false);
const editorOpen = ref(false);
const editing = ref<GroupView>();
const form = reactive({ name: "", remark: "", members: [] as string[] });

async function refresh() {
    loading.value = true;
    try {
        const [groupList, userList] = await Promise.all([listGroups(), listAllAdminUsers()]);
        groups.value = groupList;
        users.value = userList.filter(user => !user.disabled && !user.deleted_at);
    } finally {
        loading.value = false;
    }
}

function create() {
    editing.value = undefined;
    Object.assign(form, { name: "", remark: "", members: [] });
    editorOpen.value = true;
}

async function edit(group: GroupView) {
    editing.value = group;
    const members = await groupIds("members", group.gid);
    Object.assign(form, { name: group.name, remark: group.remark, members });
    editorOpen.value = true;
}

async function save() {
    if (!editing.value && (!form.name.trim() || form.name !== form.name.trim())) {
        message.error(t("identity.groups.nameRequired"));
        return;
    }
    saving.value = true;
    try {
        const group = editing.value || (await createGroup(form.name, form.remark));
        await replaceGroupIds("members", group, form.members);
        editorOpen.value = false;
        message.success(t("identity.groups.saved"));
        await refresh();
    } finally {
        saving.value = false;
    }
}

function remove(group: GroupView) {
    Modal.confirm({
        title: t("identity.groups.deleteTitle", { name: group.name }),
        content: t("identity.groups.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteGroup(group);
            await refresh();
        },
    });
}

onMounted(refresh);
useManagementRefresh(["identities"], refresh);
</script>

<template>
    <a-card :title="t('identity.groups.title')">
        <template #extra>
            <a-button type="primary" @click="create">{{ t("identity.groups.create") }}</a-button>
        </template>
        <a-table :data-source="groups" row-key="gid" :loading="loading" :pagination="false">
            <a-table-column :title="t('identity.groups.name')" data-index="name" />
            <a-table-column :title="t('identity.groups.remark')" data-index="remark" />
            <a-table-column :title="t('identity.groups.members')" data-index="member_count" />
            <a-table-column :title="t('identity.users.actions')">
                <template #default="{ record }">
                    <a-space>
                        <a-button @click="edit(record)">{{
                            t("identity.groups.editMembers")
                        }}</a-button>
                        <a-button danger @click="remove(record)">{{
                            t("identity.actions.delete")
                        }}</a-button>
                    </a-space>
                </template>
            </a-table-column>
        </a-table>
    </a-card>

    <a-modal
        v-model:open="editorOpen"
        :title="t(editing ? 'identity.groups.editMembers' : 'identity.groups.create')"
        :confirm-loading="saving"
        :ok-text="t('identity.actions.save')"
        :cancel-text="t('identity.actions.cancel')"
        width="680px"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('identity.groups.name')">
                <a-input v-model:value="form.name" :disabled="!!editing" :maxlength="128" />
            </a-form-item>
            <a-form-item :label="t('identity.groups.remark')">
                <a-textarea v-model:value="form.remark" :disabled="!!editing" :maxlength="1024" />
            </a-form-item>
            <a-form-item :label="t('identity.groups.members')">
                <a-select
                    v-model:value="form.members"
                    mode="multiple"
                    show-search
                    option-filter-prop="label"
                    :max-tag-count="6"
                    :options="users.map(user => ({ label: user.username, value: user.uid }))"
                />
            </a-form-item>
        </a-form>
    </a-modal>
</template>
