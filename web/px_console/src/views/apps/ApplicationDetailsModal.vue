<script setup lang="ts">
import { useI18n } from "vue-i18n";
import type { ManagedApplication } from "@/model/managed_application_api";

defineProps<{ application: ManagedApplication }>();
const emit = defineEmits<{ close: [] }>();
const { t } = useI18n();
</script>

<template>
    <a-modal
        :open="true"
        :title="t('activity.applicationDetails')"
        :footer="null"
        width="720px"
        @cancel="emit('close')"
    >
        <a-descriptions bordered :column="1" size="small" class="application-details">
            <a-descriptions-item :label="t('applications.name')">{{
                application.spec.name
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('applications.kind')">{{
                t(`applications.kinds.${application.spec.launch.kind}`)
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('applications.disconnectGrace')">{{
                application.spec.disconnect_grace_seconds
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('applications.access')">{{
                t(`applications.accessModes.${application.spec.access}`)
            }}</a-descriptions-item>
            <a-descriptions-item :label="t('activity.state')">{{
                t(
                    application.spec.disabled
                        ? "activity.applicationDisabled"
                        : "activity.applicationEnabled",
                )
            }}</a-descriptions-item>
            <template v-if="application.spec.launch.kind === 'game_hook'">
                <a-descriptions-item :label="t('applications.executable')">{{
                    application.spec.launch.executable_path
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('applications.arguments')">{{
                    application.spec.launch.arguments || "—"
                }}</a-descriptions-item>
            </template>
            <a-descriptions-item
                v-if="application.spec.launch.kind === 'webview'"
                :label="t('applications.entryUrl')"
                >{{ application.spec.launch.entry_url }}</a-descriptions-item
            >
            <template v-if="application.spec.launch.kind !== 'rdp'">
                <a-descriptions-item :label="t('applications.codec')">{{
                    application.spec.launch.video.codec.toUpperCase()
                }}</a-descriptions-item>
                <a-descriptions-item :label="t('applications.bitrate')"
                    >{{ application.spec.launch.video.bitrate_kbps }} Kbps</a-descriptions-item
                >
            </template>
        </a-descriptions>
    </a-modal>
</template>

<style scoped>
.application-details :deep(.ant-descriptions-item-content) {
    overflow-wrap: anywhere;
    white-space: pre-wrap;
}
</style>
