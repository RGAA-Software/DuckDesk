<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import { useI18n } from "vue-i18n";
import {
    listManagedResourceSessions,
    type ResourceOwner,
    type ResourceSession,
} from "@/model/managed_activity_api";

const { t } = useI18n();
const sessions = ref<ResourceSession[]>([]);
const loading = ref(false);
const includeClosed = ref(false);
const visibleSessions = computed(() =>
    includeClosed.value
        ? sessions.value
        : sessions.value.filter(session => !session.closed_at && session.state !== "closed"),
);

async function refresh() {
    loading.value = true;
    try {
        sessions.value = await listManagedResourceSessions();
    } finally {
        loading.value = false;
    }
}

function owner(ownerValue: ResourceOwner) {
    return "user" in ownerValue ? ownerValue.user.user_id : ownerValue.guest.guest_id;
}

function target(session: ResourceSession) {
    return session.target.kind === "desktop"
        ? `${t("activity.desktop")}: ${session.target.device_id}`
        : `${t("activity.cloudApplication")}: ${session.target.application_id}`;
}

onMounted(refresh);
useManagementRefresh(
    ["sessions", "channels", "file_transfers", "recordings", "instances"],
    refresh,
);
</script>

<template>
    <a-card :title="t('activity.sessionsTitle')">
        <template #extra
            ><a-space
                ><a-checkbox v-model:checked="includeClosed">{{
                    t("activity.includeClosed")
                }}</a-checkbox
                ><a-button @click="refresh">{{ t("dashboard.refresh") }}</a-button></a-space
            ></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('activity.sessionNotice')"
            style="margin-bottom: 12px"
        />
        <a-table
            :data-source="visibleSessions"
            row-key="id"
            :loading="loading"
            :pagination="{ pageSize: 20 }"
        >
            <a-table-column :title="t('activity.session')" data-index="id" />
            <a-table-column :title="t('activity.owner')"
                ><template #default="{ record }">{{
                    owner(record.owner)
                }}</template></a-table-column
            >
            <a-table-column :title="t('activity.target')"
                ><template #default="{ record }">{{ target(record) }}</template></a-table-column
            >
            <a-table-column :title="t('activity.client')" data-index="client_type" />
            <a-table-column :title="t('activity.role')" data-index="access_role" />
            <a-table-column :title="t('activity.state')" data-index="state" />
            <a-table-column :title="t('activity.createdAt')"
                ><template #default="{ record }">{{
                    new Date(record.created_at).toLocaleString()
                }}</template></a-table-column
            >
            <a-table-column :title="t('activity.closedAt')"
                ><template #default="{ record }">{{
                    record.closed_at ? new Date(record.closed_at).toLocaleString() : "-"
                }}</template></a-table-column
            >
        </a-table>
    </a-card>
</template>
