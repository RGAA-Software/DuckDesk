<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import { Modal, message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { listGroups, type GroupView } from "@/model/identity_api";
import {
    createManagedApplication,
    deleteManagedApplication,
    getManagedApplicationGroups,
    listManagedApplications,
    replaceManagedApplicationGroups,
    updateManagedApplication,
    type ApplicationAccess,
    type ApplicationLaunch,
    type ManagedApplication,
    type VideoCodec,
} from "@/model/managed_application_api";

type ApplicationKind = ApplicationLaunch["kind"];

const { t } = useI18n();
const applications = ref<ManagedApplication[]>([]);
const groups = ref<GroupView[]>([]);
const loading = ref(false);
const saving = ref(false);
const editorOpen = ref(false);
const editing = ref<ManagedApplication>();
const form = reactive({
    name: "",
    kind: "game_hook" as ApplicationKind,
    access: "public" as ApplicationAccess,
    executableRelative: "",
    arguments: "",
    entryUrl: "",
    codec: "h264" as VideoCodec,
    bitrateKbps: 20_000,
    allowObserver: false,
    allowTakeover: false,
    disabled: false,
    groupIds: [] as string[],
});

const requiresVideo = computed(() => form.kind !== "rdp");

async function refresh() {
    loading.value = true;
    try {
        [applications.value, groups.value] = await Promise.all([
            listManagedApplications(),
            listGroups(),
        ]);
    } finally {
        loading.value = false;
    }
}

function resetForm() {
    Object.assign(form, {
        name: "",
        kind: "game_hook",
        access: "public",
        executableRelative: "",
        arguments: "",
        entryUrl: "",
        codec: "h264",
        bitrateKbps: 20_000,
        allowObserver: false,
        allowTakeover: false,
        disabled: false,
        groupIds: [],
    });
}

function create() {
    editing.value = undefined;
    resetForm();
    editorOpen.value = true;
}

async function edit(application: ManagedApplication) {
    editing.value = application;
    const launch = application.spec.launch;
    Object.assign(form, {
        name: application.spec.name,
        kind: launch.kind,
        access: application.spec.access,
        executableRelative: launch.kind === "game_hook" ? launch.executable_relative : "",
        arguments: launch.kind === "game_hook" ? launch.arguments : "",
        entryUrl: launch.kind === "webview" ? launch.entry_url : "",
        codec: launch.kind === "rdp" ? "h264" : launch.video.codec,
        bitrateKbps: launch.kind === "rdp" ? 20_000 : launch.video.bitrate_kbps,
        allowObserver: application.spec.allow_observer,
        allowTakeover: application.spec.allow_takeover,
        disabled: application.spec.disabled,
        groupIds: await getManagedApplicationGroups(application),
    });
    editorOpen.value = true;
}

function launch(): ApplicationLaunch {
    const video = { codec: form.codec, bitrate_kbps: form.bitrateKbps };
    if (form.kind === "game_hook") {
        return {
            kind: "game_hook",
            executable_relative: form.executableRelative,
            arguments: form.arguments,
            video,
        };
    }
    if (form.kind === "webview") return { kind: "webview", entry_url: form.entryUrl, video };
    return { kind: "rdp" };
}

async function save() {
    if (!form.name.trim() || form.name !== form.name.trim()) {
        message.error(t("applications.validation.name"));
        return;
    }
    if (form.access === "acl" && form.groupIds.length === 0) {
        message.error(t("applications.validation.groups"));
        return;
    }
    saving.value = true;
    try {
        const spec = {
            name: form.name,
            access: form.access,
            launch: launch(),
            allow_observer: form.kind === "rdp" ? false : form.allowObserver,
            allow_takeover: form.kind === "rdp" ? false : form.allowTakeover,
            disabled: form.disabled,
        };
        let application = editing.value
            ? await updateManagedApplication(editing.value, spec)
            : await createManagedApplication(spec);
        application = await replaceManagedApplicationGroups(
            application,
            form.access === "acl" ? form.groupIds : [],
        );
        editorOpen.value = false;
        message.success(t("applications.messages.saved"));
        await refresh();
    } finally {
        saving.value = false;
    }
}

function remove(application: ManagedApplication) {
    Modal.confirm({
        title: t("applications.confirm.deleteTitle", { name: application.spec.name }),
        content: t("applications.confirm.deleteImpact"),
        okType: "danger",
        async onOk() {
            await deleteManagedApplication(application);
            await refresh();
        },
    });
}

onMounted(refresh);
useManagementRefresh(["applications"], refresh);
</script>

<template>
    <a-card :title="t('applications.catalogTitle')">
        <template #extra
            ><a-button type="primary" @click="create">{{
                t("applications.create")
            }}</a-button></template
        >
        <a-table :data-source="applications" row-key="id" :loading="loading" :pagination="false">
            <a-table-column :title="t('applications.name')"
                ><template #default="{ record }">{{ record.spec.name }}</template></a-table-column
            >
            <a-table-column :title="t('applications.kind')"
                ><template #default="{ record }">{{
                    t(`applications.kinds.${record.spec.launch.kind}`)
                }}</template></a-table-column
            >
            <a-table-column :title="t('applications.access')"
                ><template #default="{ record }">{{
                    t(`applications.accessModes.${record.spec.access}`)
                }}</template></a-table-column
            >
            <a-table-column :title="t('identity.users.status')"
                ><template #default="{ record }"
                    ><a-tag :color="record.spec.disabled ? 'red' : 'green'">{{
                        t(
                            record.spec.disabled
                                ? "identity.states.disabled"
                                : "identity.states.active",
                        )
                    }}</a-tag></template
                ></a-table-column
            >
            <a-table-column :title="t('identity.users.actions')"
                ><template #default="{ record }"
                    ><a-space
                        ><a-button size="small" @click="edit(record)">{{
                            t("identity.actions.edit")
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
        :title="t(editing ? 'applications.edit' : 'applications.create')"
        :confirm-loading="saving"
        width="720px"
        @ok="save"
    >
        <a-form layout="vertical">
            <a-form-item :label="t('applications.name')"
                ><a-input v-model:value="form.name" :maxlength="128"
            /></a-form-item>
            <a-form-item :label="t('applications.kind')"
                ><a-select
                    v-model:value="form.kind"
                    :disabled="!!editing"
                    :options="
                        (['game_hook', 'webview', 'rdp'] as const).map(kind => ({
                            value: kind,
                            label: t(`applications.kinds.${kind}`),
                        }))
                    "
            /></a-form-item>
            <a-form-item v-if="form.kind === 'game_hook'" :label="t('applications.executable')"
                ><a-input v-model:value="form.executableRelative"
            /></a-form-item>
            <a-form-item v-if="form.kind === 'game_hook'" :label="t('applications.arguments')"
                ><a-textarea v-model:value="form.arguments"
            /></a-form-item>
            <a-form-item v-if="form.kind === 'webview'" :label="t('applications.entryUrl')"
                ><a-input v-model:value="form.entryUrl"
            /></a-form-item>
            <a-row v-if="requiresVideo" :gutter="16"
                ><a-col :span="12"
                    ><a-form-item :label="t('applications.codec')"
                        ><a-select
                            v-model:value="form.codec"
                            :options="[
                                { value: 'h264', label: 'H.264' },
                                { value: 'h265', label: 'H.265' },
                            ]" /></a-form-item></a-col
                ><a-col :span="12"
                    ><a-form-item :label="t('applications.bitrate')"
                        ><a-input-number
                            v-model:value="form.bitrateKbps"
                            :min="128"
                            :max="200000"
                            class="w-full" /></a-form-item></a-col
            ></a-row>
            <a-row v-if="requiresVideo" :gutter="16"
                ><a-col :span="12"
                    ><a-form-item :label="t('applications.allowObserver')"
                        ><a-switch v-model:checked="form.allowObserver" /></a-form-item></a-col
                ><a-col :span="12"
                    ><a-form-item :label="t('applications.allowTakeover')"
                        ><a-switch v-model:checked="form.allowTakeover" /></a-form-item></a-col
            ></a-row>
            <a-form-item :label="t('applications.access')"
                ><a-radio-group v-model:value="form.access"
                    ><a-radio value="public">{{ t("applications.accessModes.public") }}</a-radio
                    ><a-radio value="acl">{{
                        t("applications.accessModes.acl")
                    }}</a-radio></a-radio-group
                ></a-form-item
            >
            <a-form-item v-if="form.access === 'acl'" :label="t('applications.groups')"
                ><a-select
                    v-model:value="form.groupIds"
                    mode="multiple"
                    :options="groups.map(group => ({ label: group.name, value: group.gid }))"
            /></a-form-item>
            <a-form-item :label="t('applications.disabled')"
                ><a-switch v-model:checked="form.disabled"
            /></a-form-item>
        </a-form>
    </a-modal>
</template>
