<script setup lang="ts">
import { message } from "ant-design-vue";
import { onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import {
    listManagedChannels,
    listManagedFileTransfers,
    listManagedRecordingCache,
    listManagedRecordings,
    listManagedVisits,
    downloadManagedRecording,
    evictManagedRecordingCache,
    requestManagedRecordingCache,
    updateManagedRecordingRetention,
    type ChannelRecord,
    type FileTransferRecord,
    type RecordingProfile,
    type RecordingCacheProfile,
    type VisitRecord,
} from "@/model/managed_activity_api";

const { t } = useI18n();
const loading = ref(false);
const visits = ref<VisitRecord[]>([]);
const channels = ref<ChannelRecord[]>([]);
const transfers = ref<FileTransferRecord[]>([]);
const recordings = ref<RecordingProfile[]>([]);
const recordingCache = ref<Record<string, RecordingCacheProfile>>({});
const recordingAction = ref("");

async function refresh() {
    loading.value = true;
    try {
        const [visitRows, channelRows, transferRows, recordingRows, cacheRows] = await Promise.all([
            listManagedVisits(),
            listManagedChannels(),
            listManagedFileTransfers(),
            listManagedRecordings(),
            listManagedRecordingCache(),
        ]);
        visits.value = visitRows;
        channels.value = channelRows;
        transfers.value = transferRows;
        recordings.value = recordingRows;
        recordingCache.value = Object.fromEntries(
            cacheRows.map(profile => [profile.recording_id, profile]),
        );
    } finally {
        loading.value = false;
    }
}

function bytes(value: number) {
    if (value >= 1024 * 1024 * 1024) return `${(value / (1024 * 1024 * 1024)).toFixed(2)} GB`;
    if (value >= 1024 * 1024) return `${(value / (1024 * 1024)).toFixed(2)} MB`;
    if (value >= 1024) return `${(value / 1024).toFixed(2)} KB`;
    return `${value} B`;
}

function cacheFor(recording: RecordingProfile) {
    return recordingCache.value[recording.id];
}

async function downloadRecording(recording: RecordingProfile) {
    recordingAction.value = recording.id;
    try {
        const cache = await requestManagedRecordingCache(recording.id);
        recordingCache.value[recording.id] = cache;
        if (cache.state !== "ready") {
            message.info(t("activity.recordingPreparing"));
            return;
        }
        const blob = await downloadManagedRecording(recording.id);
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = recording.file_name;
        document.body.append(anchor);
        anchor.click();
        anchor.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 0);
    } catch {
        message.error(t("activity.recordingDownloadFailed"));
    } finally {
        recordingAction.value = "";
    }
}

async function setRecordingRetention(recording: RecordingProfile, retained: boolean) {
    const current = recordingCache.value[recording.id];
    if (!current) return;
    recordingAction.value = recording.id;
    try {
        const changed = await updateManagedRecordingRetention(
            recording.id,
            current.revision,
            retained,
        );
        recordingCache.value[recording.id] = changed;
        message.success(t(retained ? "activity.recordingRetained" : "activity.recordingReleased"));
    } catch {
        message.error(t("activity.recordingCacheActionFailed"));
    } finally {
        recordingAction.value = "";
    }
}

async function evictRecording(recording: RecordingProfile) {
    const current = recordingCache.value[recording.id];
    if (!current || current.pinned) return;
    recordingAction.value = recording.id;
    try {
        await evictManagedRecordingCache(recording.id, current.revision);
        delete recordingCache.value[recording.id];
        message.success(t("activity.recordingEvicted"));
    } catch {
        message.error(t("activity.recordingCacheActionFailed"));
    } finally {
        recordingAction.value = "";
    }
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('activity.auditTitle')">
        <template #extra
            ><a-button @click="refresh">{{ t("dashboard.refresh") }}</a-button></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('activity.auditNotice')"
            style="margin-bottom: 12px"
        />
        <a-tabs>
            <a-tab-pane key="visits" :tab="t('activity.visits')">
                <a-table
                    :data-source="visits"
                    :loading="loading"
                    row-key="session.id"
                    :pagination="{ pageSize: 20 }"
                >
                    <a-table-column :title="t('activity.session')"
                        ><template #default="{ record }">{{
                            record.session.id
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.node')" data-index="node_id" />
                    <a-table-column :title="t('activity.client')"
                        ><template #default="{ record }">{{
                            record.session.client_type
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.state')"
                        ><template #default="{ record }">{{
                            record.session.state
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.channels')" data-index="channel_count" />
                    <a-table-column :title="t('activity.connectedAt')"
                        ><template #default="{ record }">{{
                            record.first_connected_at
                                ? new Date(record.first_connected_at).toLocaleString()
                                : "-"
                        }}</template></a-table-column
                    >
                </a-table>
            </a-tab-pane>
            <a-tab-pane key="channels" :tab="t('activity.channels')">
                <a-table
                    :data-source="channels"
                    :loading="loading"
                    row-key="id"
                    :pagination="{ pageSize: 20 }"
                >
                    <a-table-column :title="t('activity.kind')" data-index="kind" />
                    <a-table-column :title="t('activity.session')" data-index="session_id" />
                    <a-table-column :title="t('activity.state')" data-index="state" />
                    <a-table-column :title="t('activity.sent')"
                        ><template #default="{ record }">{{
                            bytes(record.sent_bytes)
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.received')"
                        ><template #default="{ record }">{{
                            bytes(record.received_bytes)
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.reason')"
                        ><template #default="{ record }">{{
                            record.reason || "-"
                        }}</template></a-table-column
                    >
                </a-table>
            </a-tab-pane>
            <a-tab-pane key="transfers" :tab="t('activity.transfers')">
                <a-table
                    :data-source="transfers"
                    :loading="loading"
                    row-key="id"
                    :pagination="{ pageSize: 20 }"
                >
                    <a-table-column :title="t('activity.file')" data-index="file_name" />
                    <a-table-column :title="t('activity.direction')" data-index="direction" />
                    <a-table-column :title="t('activity.state')" data-index="state" />
                    <a-table-column :title="t('activity.progress')"
                        ><template #default="{ record }"
                            >{{ bytes(record.transferred_bytes) }} /
                            {{ bytes(record.total_bytes) }}</template
                        ></a-table-column
                    >
                    <a-table-column :title="t('activity.updatedAt')"
                        ><template #default="{ record }">{{
                            new Date(record.updated_at).toLocaleString()
                        }}</template></a-table-column
                    >
                </a-table>
            </a-tab-pane>
            <a-tab-pane key="recordings" :tab="t('activity.recordings')">
                <a-table
                    :data-source="recordings"
                    :loading="loading"
                    row-key="id"
                    :pagination="{ pageSize: 20 }"
                >
                    <a-table-column :title="t('activity.file')" data-index="file_name" />
                    <a-table-column :title="t('activity.node')" data-index="node_id" />
                    <a-table-column :title="t('activity.codec')" data-index="codec" />
                    <a-table-column :title="t('activity.size')"
                        ><template #default="{ record }">{{
                            bytes(record.size_bytes)
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.present')"
                        ><template #default="{ record }">{{
                            record.reported_present
                                ? t("activity.reportedPresent")
                                : t("activity.reportedMissing")
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.recordingCache')">
                        <template #default="{ record }">
                            <a-tag v-if="cacheFor(record)">
                                {{ cacheFor(record)?.state }}
                            </a-tag>
                            <span v-else>{{ t("activity.recordingNotCached") }}</span>
                        </template>
                    </a-table-column>
                    <a-table-column :title="t('activity.observedAt')"
                        ><template #default="{ record }">{{
                            new Date(record.observed_at).toLocaleString()
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.action')">
                        <template #default="{ record }">
                            <a-space>
                                <a-button
                                    type="link"
                                    :loading="recordingAction === record.id"
                                    @click="downloadRecording(record)"
                                >
                                    {{ t("activity.download") }}
                                </a-button>
                                <a-button
                                    v-if="cacheFor(record)?.state === 'ready'"
                                    type="link"
                                    :disabled="recordingAction === record.id"
                                    @click="
                                        setRecordingRetention(
                                            record,
                                            !(cacheFor(record)?.pinned ?? false),
                                        )
                                    "
                                >
                                    {{
                                        t(
                                            cacheFor(record)?.pinned
                                                ? "activity.recordingRelease"
                                                : "activity.recordingRetain",
                                        )
                                    }}
                                </a-button>
                                <a-popconfirm
                                    v-if="cacheFor(record)?.state === 'ready'"
                                    :title="t('activity.recordingEvictConfirm')"
                                    :disabled="cacheFor(record)?.pinned === true"
                                    @confirm="evictRecording(record)"
                                >
                                    <a-button
                                        type="link"
                                        danger
                                        :disabled="
                                            cacheFor(record)?.pinned === true ||
                                            recordingAction === record.id
                                        "
                                    >
                                        {{ t("activity.recordingEvict") }}
                                    </a-button>
                                </a-popconfirm>
                            </a-space>
                        </template>
                    </a-table-column>
                </a-table>
            </a-tab-pane>
        </a-tabs>
    </a-card>
</template>
