<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
import { listAllAdminUsers } from "@/model/identity_api";
import { listManagedApplications } from "@/model/managed_application_api";
import { listManagedResourceSessions, type ResourceSession } from "@/model/managed_activity_api";
import { listManagedDeployments } from "@/model/managed_deployment_api";
import { listManagedDevices } from "@/model/managed_device_api";
import { listManagedNodes } from "@/model/managed_node_api";

const { t } = useI18n();
const loading = ref(false);
const sessions = ref<ResourceSession[]>([]);
const totals = ref({
    devices: 0,
    users: 0,
    applications: 0,
    deployments: 0,
    nodes: 0,
    freshNodes: 0,
});

const activeSessions = computed(() =>
    sessions.value.filter(session => !session.closed_at && session.state !== "closed"),
);
const recentSessions = computed(() => sessions.value.slice(0, 10));

async function refresh() {
    loading.value = true;
    try {
        const [devices, users, applications, deployments, nodes, resourceSessions] =
            await Promise.all([
                listManagedDevices(),
                listAllAdminUsers(),
                listManagedApplications(),
                listManagedDeployments(),
                listManagedNodes(),
                listManagedResourceSessions(),
            ]);
        totals.value = {
            devices: devices.length,
            users: users.length,
            applications: applications.length,
            deployments: deployments.length,
            nodes: nodes.length,
            freshNodes: nodes.filter(node => node.fresh && !node.disabled).length,
        };
        sessions.value = resourceSessions;
    } finally {
        loading.value = false;
    }
}

function target(session: ResourceSession) {
    return session.target.kind === "desktop"
        ? session.target.device_id
        : session.target.application_id;
}

onMounted(refresh);
</script>

<template>
    <a-spin :spinning="loading">
        <a-space direction="vertical" size="large" class="w-full">
            <a-row :gutter="16">
                <a-col :span="4"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.devices')"
                            :value="totals.devices" /></a-card
                ></a-col>
                <a-col :span="4"
                    ><a-card
                        ><a-statistic :title="t('dashboard.users')" :value="totals.users" /></a-card
                ></a-col>
                <a-col :span="4"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.applications')"
                            :value="totals.applications" /></a-card
                ></a-col>
                <a-col :span="4"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.deployments')"
                            :value="totals.deployments" /></a-card
                ></a-col>
                <a-col :span="4"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.nodes')"
                            :value="`${totals.freshNodes}/${totals.nodes}`" /></a-card
                ></a-col>
                <a-col :span="4"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.activeSessions')"
                            :value="activeSessions.length" /></a-card
                ></a-col>
            </a-row>
            <a-card :title="t('dashboard.recentSessions')">
                <template #extra
                    ><a-button @click="refresh">{{ t("dashboard.refresh") }}</a-button></template
                >
                <a-table :data-source="recentSessions" row-key="id" :pagination="false">
                    <a-table-column :title="t('activity.session')" data-index="id" />
                    <a-table-column :title="t('activity.target')"
                        ><template #default="{ record }">{{
                            target(record)
                        }}</template></a-table-column
                    >
                    <a-table-column :title="t('activity.client')" data-index="client_type" />
                    <a-table-column :title="t('activity.role')" data-index="access_role" />
                    <a-table-column :title="t('activity.state')" data-index="state" />
                    <a-table-column :title="t('activity.createdAt')"
                        ><template #default="{ record }">{{
                            new Date(record.created_at).toLocaleString()
                        }}</template></a-table-column
                    >
                </a-table>
            </a-card>
        </a-space>
    </a-spin>
</template>
