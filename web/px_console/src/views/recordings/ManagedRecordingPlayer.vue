<script setup lang="ts">
import { watch } from "vue";
import { useI18n } from "vue-i18n";
import type { RecordingProfile } from "@/model/managed_activity_api";
import { useManagedRecordingPreview } from "@/composables/useManagedRecordingPreview";

const props = defineProps<{ recording: RecordingProfile }>();
const emit = defineEmits<{ close: [] }>();
const { t } = useI18n();
const { phase, progress, source, errorKey, isOpen, open, close, playbackFailed } =
    useManagedRecordingPreview();
watch(
    () => props.recording,
    selectedRecording => void open(selectedRecording),
    { immediate: true },
);

function closePlayer() {
    close();
    emit("close");
}
</script>

<template>
    <a-modal
        :open="isOpen"
        :title="recording.file_name"
        :footer="null"
        width="min(1000px, 96vw)"
        destroy-on-close
        @cancel="closePlayer"
    >
        <template v-if="phase === 'preparing' || phase === 'loading'">
            <p role="status">
                {{
                    t(
                        phase === "preparing"
                            ? "recordingPreview.preparing"
                            : "recordingPreview.loading",
                    )
                }}
            </p>
            <a-progress :percent="progress" />
        </template>
        <template v-else-if="phase === 'error'">
            <a-alert type="error" show-icon :message="t(errorKey)" />
            <a-button style="margin-top: 12px" @click="open(recording)">{{
                t("recordingPreview.retry")
            }}</a-button>
        </template>
        <video
            v-else-if="phase === 'ready'"
            :key="source"
            :src="source"
            :aria-label="recording.file_name"
            controls
            autoplay
            playsinline
            preload="metadata"
            style="width: 100%; max-height: 70vh"
            @error="playbackFailed"
        />
    </a-modal>
</template>
