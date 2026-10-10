<script setup lang="ts">
import { computed, onMounted, onScopeDispose, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events";
import { useI18n } from "vue-i18n";
import {
    listManagedResourceSessions,
    type ResourceOwner,
    type ResourceSession,
} from "@/model/managed_activity_api";
import { listManagedApplications, type ManagedApplication } from "@/model/managed_application_api";
import { listManagedDevices, type ManagedDevice } from "@/model/managed_device_api";
import { listAllAdminUsers, type UserAdminView } from "@/model/identity_api";
import ApplicationDetailsModal from "@/views/apps/ApplicationDetailsModal.vue";

const { t, locale } = useI18n();
const sessions = ref<ResourceSession[]>([]);
const applications = ref<ManagedApplication[]>([]);
const devices = ref<ManagedDevice[]>([]);
const users = ref<UserAdminView[]>([]);
const loading = ref(false);
const loadFailed = ref(false);
const selectedApplicationId = ref<string>();
const applicationMap = computed(
    () => new Map(applications.value.map(application => [application.id, application])),
);
const deviceMap = computed(() => new Map(devices.value.map(device => [device.id, device])));
const userMap = computed(() => new Map(users.value.map(user => [user.uid, user])));
const selectedApplication = computed(() =>
    selectedApplicationId.value ? applicationMap.value.get(selectedApplicationId.value) : undefined,
);
const visibleSessions = computed(() =>
    sessions.value
        .filter(session => !session.closed_at && session.state !== "closed")
        .sort(
            (firstSession, secondSession) =>
                Date.parse(secondSession.created_at) - Date.parse(firstSession.created_at),
        ),
);
const columns = computed(() => [
    { title: t("activity.session"), key: "session", dataIndex: "id", width: 280, ellipsis: true },
    { title: t("activity.owner"), key: "owner", width: 150 },
    { title: t("activity.target"), key: "target", width: 240 },
    { title: t("activity.client"), key: "client", width: 140 },
    { title: t("activity.role"), key: "role", width: 120 },
    { title: t("activity.state"), key: "state", width: 110 },
    { title: t("activity.createdAt"), key: "createdAt", width: 210 },
]);
let disposed = false;
let refreshTimer: ReturnType<typeof setInterval> | undefined;

async function refresh() {
    if (disposed || loading.value) return;
    loading.value = true;
    try {
        const [sessionRows, applicationRows, deviceRows, userRows] = await Promise.all([
            listManagedResourceSessions(),
            listManagedApplications(),
            listManagedDevices(),
            listAllAdminUsers(),
        ]);
        if (disposed) return;
        sessions.value = sessionRows;
        applications.value = applicationRows;
        devices.value = deviceRows;
        users.value = userRows;
        loadFailed.value = false;
    } catch {
        if (!disposed) loadFailed.value = true;
    } finally {
        if (!disposed) loading.value = false;
    }
}

function ownerName(owner: ResourceOwner): string {
    return owner.kind === "user"
        ? userMap.value.get(owner.user_id)?.username || t("activity.unknownUser")
        : t("activity.guest");
}
function targetName(session: ResourceSession): string {
    return session.target.kind === "desktop"
        ? deviceMap.value.get(session.target.device_id)?.name || t("activity.unavailableDevice")
        : applicationMap.value.get(session.target.application_id)?.spec.name ||
              t("activity.unavailableApplication");
}
function localizedValue(category: "clients" | "roles" | "sessionStates", value: string): string {
    const knownValues = {
        clients: ["panel", "android", "user_web", "admin_web"],
        roles: ["controller", "observer", "file_transfer"],
        sessionStates: ["pending", "connected", "closed"],
    };
    return t(`activity.${category}.${knownValues[category].includes(value) ? value : "unknown"}`);
}
function createdAt(timestamp: string): string {
    const parsedTime = new Date(timestamp);
    if (!Number.isFinite(parsedTime.getTime())) return "—";
    return new Intl.DateTimeFormat(locale.value, {
        dateStyle: "medium",
        timeStyle: "medium",
        hour12: false,
    }).format(parsedTime);
}

onMounted(() => {
    void refresh();
    refreshTimer = setInterval(() => void refresh(), 15_000);
});
useManagementRefresh(
    ["sessions", "channels", "instances", "applications", "devices", "identities"],
    refresh,
);
onScopeDispose(() => {
    disposed = true;
    clearInterval(refreshTimer);
});
</script>

<template>
    <a-card :title="t('activity.sessionsTitle')" class="online-connections">
        <template #extra
            ><a-button :loading="loading" @click="refresh">{{
                t("dashboard.refresh")
            }}</a-button></template
        >
        <a-alert
            :type="loadFailed ? 'warning' : 'info'"
            show-icon
            :message="t(loadFailed ? 'activity.sessionsLoadFailed' : 'activity.sessionNotice')"
            class="session-notice"
        />
        <a-table
            :columns="columns"
            :data-source="visibleSessions"
            row-key="id"
            :loading="loading"
            :pagination="{ pageSize: 20 }"
            :scroll="{ x: 1250 }"
        >
            <template #bodyCell="{ column, record }">
                <template v-if="column.key === 'owner'">{{ ownerName(record.owner) }}</template>
                <template v-else-if="column.key === 'target'">
                    <span class="target-kind">{{
                        t(
                            record.target.kind === "desktop"
                                ? "activity.desktop"
                                : "activity.cloudApplication",
                        )
                    }}</span>
                    <a-button
                        v-if="
                            record.target.kind === 'cloud_application' &&
                            applicationMap.has(record.target.application_id)
                        "
                        type="link"
                        class="application-target"
                        @click="selectedApplicationId = record.target.application_id"
                        >{{ targetName(record) }}</a-button
                    >
                    <span v-else>{{ targetName(record) }}</span>
                </template>
                <template v-else-if="column.key === 'client'">{{
                    localizedValue("clients", record.client_type)
                }}</template>
                <template v-else-if="column.key === 'role'">{{
                    localizedValue("roles", record.access_role)
                }}</template>
                <template v-else-if="column.key === 'state'"
                    ><a-tag :color="record.state === 'connected' ? 'green' : 'default'">{{
                        localizedValue("sessionStates", record.state)
                    }}</a-tag></template
                >
                <template v-else-if="column.key === 'createdAt'">{{
                    createdAt(record.created_at)
                }}</template>
            </template>
        </a-table>
        <ApplicationDetailsModal
            v-if="selectedApplication"
            :application="selectedApplication"
            @close="selectedApplicationId = undefined"
        />
    </a-card>
</template>

<style scoped>
.session-notice {
    margin-bottom: 12px;
}
.target-kind {
    display: block;
    font-size: 12px;
    opacity: 0.65;
}
.application-target {
    height: auto;
    padding: 0;
    white-space: normal;
    text-align: start;
}
</style>
