<script setup lang="ts">
import { message, Modal } from "ant-design-vue";
import { onMounted, onUnmounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import {
    getFileTransfersPage,
    getInstancesPage,
    openInstance,
    stopInstance,
    type FileTransferView,
    type InstanceView,
} from "./api";

const { t, te } = useI18n();
const instances = ref<InstanceView[]>([]);
const transfers = ref<FileTransferView[]>([]);
const loading = ref(false);
const instancePage = ref(1);
const instancePageSize = ref(10);
const instanceTotal = ref(0);
const instanceKeyword = ref("");
const instanceState = ref("");
const transferPage = ref(1);
const transferPageSize = ref(10);
const transferTotal = ref(0);
const transferKeyword = ref("");
const transferState = ref("");
let refreshing = false;
let timer = 0;

async function refresh(showLoading = false) {
    if (refreshing) return;
    refreshing = true;
    if (showLoading) loading.value = true;
    try {
        const [instanceResult, transferResult] = await Promise.all([
            getInstancesPage(
                instancePage.value,
                instancePageSize.value,
                instanceKeyword.value,
                instanceState.value,
            ),
            getFileTransfersPage(
                transferPage.value,
                transferPageSize.value,
                transferKeyword.value,
                transferState.value,
            ),
        ]);
        instances.value = instanceResult.items;
        instanceTotal.value = instanceResult.total;
        transfers.value = transferResult.items;
        transferTotal.value = transferResult.total;
    }
    catch {
        if (showLoading) message.error(t("userPortal.activity.loadFailed"));
    }
    finally {
        refreshing = false;
        loading.value = false;
    }
}

function stop(item: InstanceView) {
    Modal.confirm({
        title: t("userPortal.activity.stopTitle", { name: item.app_name }),
        content: t("userPortal.activity.stopNotice"),
        okType: "danger",
        async onOk() {
            try {
                await stopInstance(item.instance_id);
                await refresh();
            }
            catch {
                message.error(t("userPortal.activity.stopFailed"));
            }
        },
    });
}

function searchInstances() {
    instancePage.value = 1;
    void refresh(true);
}

function searchTransfers() {
    transferPage.value = 1;
    void refresh(true);
}

function changeInstancePage(pagination: { current?: number; pageSize?: number }) {
    instancePage.value = pagination.current || 1;
    instancePageSize.value = pagination.pageSize || 10;
    void refresh(true);
}

function changeTransferPage(pagination: { current?: number; pageSize?: number }) {
    transferPage.value = pagination.current || 1;
    transferPageSize.value = pagination.pageSize || 10;
    void refresh(true);
}

function formatTime(value?: string | null) {
    return value ? new Date(value).toLocaleString() : "—";
}

function formatBytes(value: number) {
    if (value >= 1024 * 1024 * 1024) return `${(value / (1024 * 1024 * 1024)).toFixed(2)} GB`;
    if (value >= 1024 * 1024) return `${(value / (1024 * 1024)).toFixed(2)} MB`;
    if (value >= 1024) return `${(value / 1024).toFixed(2)} KB`;
    return `${value} B`;
}

function transferReason(reason: string | null) {
    if (!reason) return "—";
    const key = `userPortal.activity.transferReasons.${reason}`;
    return te(key) ? t(key) : reason;
}

onMounted(() => {
    void refresh(true);
    timer = window.setInterval(() => void refresh(), 5000);
});
onUnmounted(() => window.clearInterval(timer));
</script>

<template>
    <a-card :title="t('userPortal.activity.title')">
        <template #extra>
            <a-button :loading="loading" @click="refresh(true)">
                {{ t("userPortal.activity.refresh") }}
            </a-button>
        </template>
        <a-tabs>
            <a-tab-pane key="instances" :tab="t('userPortal.activity.instances')">
                <a-space style="margin-bottom: 16px">
                    <a-input-search
                        v-model:value="instanceKeyword"
                        allow-clear
                        :placeholder="t('userPortal.activity.instanceSearch')"
                        @search="searchInstances"
                    />
                    <a-select
                        v-model:value="instanceState"
                        style="width: 140px"
                        :options="[
                            { label: t('userPortal.activity.allStates'), value: '' },
                            { label: t('userPortal.activity.states.starting'), value: 'starting' },
                            { label: t('userPortal.activity.states.running'), value: 'running' },
                            { label: t('userPortal.activity.states.stopping'), value: 'stopping' },
                            { label: t('userPortal.activity.states.stopped'), value: 'stopped' },
                            { label: t('userPortal.activity.states.failed'), value: 'failed' },
                        ]"
                        @change="searchInstances"
                    />
                </a-space>
                <a-table
                    :data-source="instances"
                    :loading="loading"
                    row-key="instance_id"
                    :pagination="{
                        current: instancePage,
                        pageSize: instancePageSize,
                        total: instanceTotal,
                        showSizeChanger: true,
                        showTotal: (value: number) =>
                            t('userPortal.activity.instanceTotal', { value }),
                    }"
                    :scroll="{ x: 1100 }"
                    @change="changeInstancePage"
                >
                    <a-table-column
                        :title="t('userPortal.activity.application')"
                        data-index="app_name"
                    />
                    <a-table-column :title="t('userPortal.activity.state')">
                        <template #default="{ record }">
                            {{ t(`userPortal.activity.states.${record.state}`) }}
                        </template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.createdAt')" width="180">
                        <template #default="{ record }">{{
                            formatTime(record.created_at)
                        }}</template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.stoppedAt')" width="180">
                        <template #default="{ record }">{{
                            formatTime(record.stopped_at)
                        }}</template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.action')">
                        <template #default="{ record }">
                            <a-space>
                                <a-button
                                    v-if="record.reconnectable"
                                    @click="openInstance(record, undefined, true)"
                                >
                                    {{ t("userPortal.activity.viewOnly") }}
                                </a-button>
                                <a-button v-if="record.reconnectable" @click="openInstance(record)">
                                    {{ t("userPortal.activity.enter") }}
                                </a-button>
                                <a-button
                                    danger
                                    :disabled="['stopped', 'failed'].includes(record.state)"
                                    @click="stop(record)"
                                >
                                    {{ t("userPortal.activity.stop") }}
                                </a-button>
                            </a-space>
                        </template>
                    </a-table-column>
                </a-table>
            </a-tab-pane>
            <a-tab-pane key="transfers" :tab="t('userPortal.activity.transfers')">
                <a-alert
                    type="info"
                    show-icon
                    :message="t('userPortal.activity.transferNotice')"
                    style="margin-bottom: 16px"
                />
                <a-space style="margin-bottom: 16px">
                    <a-input-search
                        v-model:value="transferKeyword"
                        allow-clear
                        :placeholder="t('userPortal.activity.transferSearch')"
                        @search="searchTransfers"
                    />
                    <a-select
                        v-model:value="transferState"
                        style="width: 140px"
                        :options="[
                            { label: t('userPortal.activity.allStates'), value: '' },
                            { label: t('userPortal.activity.states.active'), value: 'active' },
                            {
                                label: t('userPortal.activity.states.completed'),
                                value: 'completed',
                            },
                            {
                                label: t('userPortal.activity.states.cancelled'),
                                value: 'cancelled',
                            },
                            { label: t('userPortal.activity.states.failed'), value: 'failed' },
                        ]"
                        @change="searchTransfers"
                    />
                </a-space>
                <a-table
                    :data-source="transfers"
                    :loading="loading"
                    row-key="id"
                    :pagination="{
                        current: transferPage,
                        pageSize: transferPageSize,
                        total: transferTotal,
                        showSizeChanger: true,
                        showTotal: (value: number) =>
                            t('userPortal.activity.transferTotal', { value }),
                    }"
                    :scroll="{ x: 1200 }"
                    @change="changeTransferPage"
                >
                    <a-table-column :title="t('userPortal.activity.file')" data-index="file_name" />
                    <a-table-column :title="t('userPortal.activity.direction')">
                        <template #default="{ record }">
                            {{ t(`userPortal.activity.directions.${record.direction}`) }}
                        </template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.state')">
                        <template #default="{ record }">
                            {{ t(`userPortal.activity.states.${record.state}`) }}
                        </template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.progress')">
                        <template #default="{ record }">
                            {{ formatBytes(record.transferred_bytes) }} /
                            {{ formatBytes(record.total_bytes) }}
                        </template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.reason')">
                        <template #default="{ record }">{{
                            transferReason(record.reason)
                        }}</template>
                    </a-table-column>
                    <a-table-column :title="t('userPortal.activity.updatedAt')" width="180">
                        <template #default="{ record }">{{
                            formatTime(record.updated_at)
                        }}</template>
                    </a-table-column>
                </a-table>
            </a-tab-pane>
        </a-tabs>
    </a-card>
</template>
