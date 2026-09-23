<script setup lang="ts">
import { onMounted, ref } from "vue";
import { message } from "ant-design-vue";
import { useI18n } from "vue-i18n";
import {
    getNodeUpdateTrustSummary,
    listManagedUpdates,
    listNodeUpdateTrustStatuses,
    supportsNodeTrust,
    type ManagedUpdateRelease,
    type NodeUpdateTrustStatus,
    type NodeUpdateTrustSummary,
} from "@/model/managed_update_api";

const { locale, t } = useI18n();
const releases = ref<ManagedUpdateRelease[]>([]);
const loading = ref(false);
const trustLoading = ref(false);
const trustOpen = ref(false);
const selectedRelease = ref<ManagedUpdateRelease>();
const trustSummary = ref<NodeUpdateTrustSummary>();
const nodeStatuses = ref<NodeUpdateTrustStatus[]>([]);

async function refresh() {
    loading.value = true;
    try {
        releases.value = await listManagedUpdates();
    }
    catch {
        message.error(t("updates.loadFailed"));
    }
    finally {
        loading.value = false;
    }
}

async function inspectTrust(release: ManagedUpdateRelease) {
    selectedRelease.value = release;
    trustSummary.value = undefined;
    nodeStatuses.value = [];
    trustOpen.value = true;
    trustLoading.value = true;
    try {
        [trustSummary.value, nodeStatuses.value] = await Promise.all([
            getNodeUpdateTrustSummary(release.id),
            listNodeUpdateTrustStatuses(release.id),
        ]);
    }
    catch {
        message.error(t("updates.trustLoadFailed"));
    }
    finally {
        trustLoading.value = false;
    }
}

function timestamp(value: string | null): string {
    if (!value) return t("updates.unknown");
    return new Intl.DateTimeFormat(locale.value, {
        dateStyle: "medium",
        timeStyle: "medium",
    }).format(new Date(value));
}

onMounted(refresh);
</script>

<template>
    <a-card :title="t('updates.title')">
        <template #extra
            ><a-button @click="refresh">{{ t("updates.refresh") }}</a-button></template
        >
        <a-alert
            type="info"
            show-icon
            :message="t('updates.scopeNotice')"
            style="margin-bottom: 12px"
        />
        <a-table :data-source="releases" row-key="id" :loading="loading" :pagination="false">
            <a-table-column :title="t('updates.product')">
                <template #default="{ record }">{{ record.artifact.target.product }}</template>
            </a-table-column>
            <a-table-column :title="t('updates.version')">
                <template #default="{ record }"
                    >{{ record.artifact.version }} ({{ record.artifact.build_number }})</template
                >
            </a-table-column>
            <a-table-column :title="t('updates.distribution')">
                <template #default="{ record }">{{ record.artifact.target.distribution }}</template>
            </a-table-column>
            <a-table-column :title="t('updates.releaseNamespace')">
                <template #default="{ record }">{{ record.artifact.target.release_namespace }}</template>
            </a-table-column>
            <a-table-column :title="t('updates.state')" data-index="state" />
            <a-table-column
                :title="t('updates.requiredRoot')"
                data-index="repository_root_version"
            />
            <a-table-column :title="t('updates.actions')">
                <template #default="{ record }">
                    <a-button
                        v-if="supportsNodeTrust(record)"
                        size="small"
                        @click="inspectTrust(record)"
                    >
                        {{ t("updates.inspectTrust") }}
                    </a-button>
                    <span v-else>{{ t("updates.noTrustProducer") }}</span>
                </template>
            </a-table-column>
            <template #emptyText>{{ t("updates.empty") }}</template>
        </a-table>
    </a-card>

    <a-modal
        v-model:open="trustOpen"
        :title="t('updates.trustTitle', { version: selectedRelease?.artifact.version || '' })"
        :footer="null"
        width="1200px"
    >
        <a-spin :spinning="trustLoading">
            <a-alert
                v-if="trustSummary"
                :type="trustSummary.unknown_or_behind_node_count === 0 ? 'success' : 'warning'"
                show-icon
                :message="
                    t('updates.trustSummary', {
                        confirmed: trustSummary.confirmed_node_count,
                        eligible: trustSummary.eligible_node_count,
                        behind: trustSummary.unknown_or_behind_node_count,
                    })
                "
                :description="t('updates.retirementNotice')"
                style="margin-bottom: 12px"
            />
            <a-table :data-source="nodeStatuses" row-key="node_id" :pagination="false" size="small">
                <a-table-column :title="t('updates.node')" data-index="node_id" />
                <a-table-column :title="t('updates.state')" data-index="node_state" />
                <a-table-column :title="t('updates.disabled')">
                    <template #default="{ record }">{{
                        record.disabled ? t("updates.yes") : t("updates.no")
                    }}</template>
                </a-table-column>
                <a-table-column :title="t('updates.lastSeen')">
                    <template #default="{ record }">{{ timestamp(record.last_seen) }}</template>
                </a-table-column>
                <a-table-column :title="t('updates.observedRoot')">
                    <template #default="{ record }">{{
                        record.trusted_root_version ?? t("updates.unknown")
                    }}</template>
                </a-table-column>
                <a-table-column :title="t('updates.observedAt')">
                    <template #default="{ record }">{{ timestamp(record.observed_at) }}</template>
                </a-table-column>
                <a-table-column :title="t('updates.confirmed')">
                    <template #default="{ record }">
                        <a-tag :color="record.confirmed ? 'green' : 'red'">
                            {{
                                record.confirmed
                                    ? t("updates.confirmedValue")
                                    : t("updates.behindValue")
                            }}
                        </a-tag>
                    </template>
                </a-table-column>
                <template #emptyText>{{ t("updates.noNodes") }}</template>
            </a-table>
        </a-spin>
    </a-modal>
</template>
