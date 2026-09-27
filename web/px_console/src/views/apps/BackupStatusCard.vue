<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from "vue";
import { message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import { getManagedBackup, triggerManagedBackup, type ManagedBackup } from "@/model/managed_backup_api";

const { locale, t } = useI18n();
const backup = ref<ManagedBackup | null>(null);
const loading = ref(false);
const triggering = ref(false);
const nowUnix = ref(Date.now() / 1000);
let statusTimer: number | undefined;
const fresh = computed(() => backup.value?.connected === true && backup.value.status !== null &&
    backup.value.reported_at_unix !== null && nowUnix.value - backup.value.reported_at_unix < 30);

async function refresh(): Promise<void> {
    loading.value = true;
    try {
        backup.value = await getManagedBackup();
    } catch {
        backup.value = null;
    } finally {
        loading.value = false;
    }
}

async function trigger(): Promise<void> {
    triggering.value = true;
    try {
        await triggerManagedBackup();
        message.success(t("backup.accepted"));
        await refresh();
    } catch {
        message.error(t("backup.triggerFailed"));
    } finally {
        triggering.value = false;
    }
}

function formatTime(timestamp: number | null | undefined): string {
    if (!timestamp) return t("backup.never");
    return new Intl.DateTimeFormat(locale.value, { dateStyle: "medium", timeStyle: "medium" }).format(new Date(timestamp * 1000));
}

onMounted(() => {
    void refresh();
    statusTimer = window.setInterval(() => {
        nowUnix.value = Date.now() / 1000;
        void refresh();
    }, 10_000);
});
onBeforeUnmount(() => window.clearInterval(statusTimer));
</script>

<template>
    <a-card :title="t('backup.title')" :loading="loading">
        <template #extra>
            <a-space>
                <a-button @click="refresh">{{ t("backup.refresh") }}</a-button>
                <a-button type="primary" :disabled="!fresh || Boolean(backup?.status?.active_task)" :loading="triggering" @click="trigger">
                    {{ t("backup.trigger") }}
                </a-button>
            </a-space>
        </template>
        <a-alert v-if="!fresh" type="warning" show-icon :message="t('backup.offline')" />
        <a-descriptions v-else bordered size="small" :column="2">
            <a-descriptions-item :label="t('backup.state')">
                {{ backup?.status?.active_task ? t("backup.running") : t("backup.idle") }}
            </a-descriptions-item>
            <a-descriptions-item :label="t('backup.lastSuccess')">{{ formatTime(backup?.status?.last_success_at_unix) }}</a-descriptions-item>
            <a-descriptions-item :label="t('backup.recoverySet')">{{ backup?.status?.last_recovery_set_id || t("backup.never") }}</a-descriptions-item>
            <a-descriptions-item :label="t('backup.lastFailure')">{{ backup?.status?.last_failure_code || t("backup.none") }}</a-descriptions-item>
        </a-descriptions>
    </a-card>
</template>
