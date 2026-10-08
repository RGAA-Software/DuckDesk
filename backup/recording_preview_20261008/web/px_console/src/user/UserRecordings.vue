<script setup lang="ts">
import { onMounted, ref } from "vue";
import { message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import {
    downloadRecording,
    getRecordingsPage,
    requestRecordingCache,
    type RecordingView,
} from "./api";

const { t } = useI18n();
const recordings = ref<RecordingView[]>([]);
const loading = ref(false);
const activeRecording = ref("");
const keyword = ref("");
const page = ref(1);
const pageSize = ref(10);
const total = ref(0);

async function refresh() {
    loading.value = true;
    try {
        const result = await getRecordingsPage(page.value, pageSize.value, keyword.value);
        recordings.value = result.items;
        total.value = result.total;
    } catch {
        message.error(t("userPortal.recordings.loadFailed"));
    } finally {
        loading.value = false;
    }
}

function search() {
    page.value = 1;
    void refresh();
}

function tableChange(pagination: { current?: number; pageSize?: number }) {
    page.value = pagination.current || 1;
    pageSize.value = pagination.pageSize || 10;
    void refresh();
}

function formatBytes(value: number) {
    const units = ["B", "KB", "MB", "GB", "TB"];
    let amount = Math.max(0, value);
    let unitIndex = 0;
    while (amount >= 1024 && unitIndex < units.length - 1) {
        amount /= 1024;
        unitIndex += 1;
    }
    return `${amount.toFixed(unitIndex === 0 ? 0 : 2)} ${units[unitIndex]}`;
}

async function saveRecording(recording: RecordingView) {
    activeRecording.value = recording.id;
    try {
        const cache = await requestRecordingCache(recording.id);
        if (cache.state !== "ready") {
            message.info(t("userPortal.recordings.preparing"));
            return;
        }
        const blob = await downloadRecording(recording.id);
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = recording.file_name;
        document.body.append(anchor);
        anchor.click();
        anchor.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 0);
    } catch {
        message.error(t("userPortal.recordings.downloadFailed"));
    } finally {
        activeRecording.value = "";
    }
}

onMounted(() => void refresh());
</script>

<template>
    <a-card :title="t('userPortal.recordings.title')">
        <template #extra>
            <a-space>
                <a-input-search
                    v-model:value="keyword"
                    allow-clear
                    :placeholder="t('userPortal.recordings.search')"
                    @search="search"
                />
                <a-button :loading="loading" @click="refresh">
                    {{ t("userPortal.recordings.refresh") }}
                </a-button>
            </a-space>
        </template>
        <a-alert class="mb-4" type="info" show-icon :message="t('userPortal.recordings.notice')" />
        <a-table
            :data-source="recordings"
            :loading="loading"
            row-key="id"
            :pagination="{
                current: page,
                pageSize,
                total,
                showSizeChanger: true,
                showTotal: (value: number) => t('userPortal.recordings.total', { value }),
            }"
            :scroll="{ x: 980 }"
            @change="tableChange"
        >
            <a-table-column :title="t('userPortal.recordings.file')" data-index="file_name" />
            <a-table-column
                :title="t('userPortal.recordings.codec')"
                data-index="codec"
                width="100"
            />
            <a-table-column :title="t('userPortal.recordings.size')" width="120">
                <template #default="{ record }">{{ formatBytes(record.size_bytes) }}</template>
            </a-table-column>
            <a-table-column :title="t('userPortal.recordings.modifiedAt')" width="190">
                <template #default="{ record }">{{
                    new Date(record.modified_at).toLocaleString()
                }}</template>
            </a-table-column>
            <a-table-column :title="t('userPortal.recordings.availability')" width="130">
                <template #default="{ record }">
                    <a-tag :color="record.reported_present ? 'green' : 'default'">
                        {{
                            t(
                                record.reported_present
                                    ? "userPortal.recordings.reportedPresent"
                                    : "userPortal.recordings.reportedMissing",
                            )
                        }}
                    </a-tag>
                </template>
            </a-table-column>
            <a-table-column :title="t('userPortal.recordings.action')" width="120">
                <template #default="{ record }">
                    <a-button
                        type="primary"
                        :loading="activeRecording === record.id"
                        :disabled="!record.reported_present"
                        @click="saveRecording(record)"
                    >
                        {{ t("userPortal.recordings.download") }}
                    </a-button>
                </template>
            </a-table-column>
        </a-table>
    </a-card>
</template>
