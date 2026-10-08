<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useManagementRefresh } from "@/model/management_events.ts";
import { useI18n } from "vue-i18n";
import { listAllAdminUsers } from "@/model/identity_api";
import { listManagedApplications } from "@/model/managed_application_api";
import { listManagedResourceSessions, type ResourceSession } from "@/model/managed_activity_api";
import { listManagedDeployments } from "@/model/managed_deployment_api";
import { listManagedDevices } from "@/model/managed_device_api";
import { listManagedNodes } from "@/model/managed_node_api";
import NodeAlertsSummary from "@/views/dashboard/NodeAlertsSummary.vue";

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

async function refresh() {
    loading.value = true;
    const [devices, users, applications, deployments, nodes, resourceSessions] = await Promise.all([
        listManagedDevices(),
        listAllAdminUsers(),
        listManagedApplications(),
        listManagedDeployments(),
        listManagedNodes(),
        listManagedResourceSessions(),
    ]).finally(() => {
        loading.value = false;
    });
    totals.value = {
        devices: devices.length,
        users: users.length,
        applications: applications.length,
        deployments: deployments.length,
        nodes: nodes.length,
        freshNodes: nodes.filter(node => node.fresh && !node.disabled).length,
    };
    sessions.value = resourceSessions;
}

onMounted(refresh);
useManagementRefresh(
    ["nodes", "instances", "sessions", "channels", "file_transfers", "recordings"],
    refresh,
);
</script>

<template>
    <a-spin :spinning="loading">
        <a-space direction="vertical" size="large" class="w-full">
            <a-row :gutter="[16, 16]">
                <a-col flex="1 1 150px"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.devices')"
                            :value="totals.devices" /></a-card
                ></a-col>
                <a-col flex="1 1 150px"
                    ><a-card
                        ><a-statistic :title="t('dashboard.users')" :value="totals.users" /></a-card
                ></a-col>
                <a-col flex="1 1 150px"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.applications')"
                            :value="totals.applications" /></a-card
                ></a-col>
                <a-col flex="1 1 150px"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.deployments')"
                            :value="totals.deployments" /></a-card
                ></a-col>
                <a-col flex="1 1 150px"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.nodes')"
                            :value="`${totals.freshNodes}/${totals.nodes}`" /></a-card
                ></a-col>
                <a-col flex="1 1 150px"
                    ><a-card
                        ><a-statistic
                            :title="t('dashboard.activeSessions')"
                            :value="activeSessions.length" /></a-card
                ></a-col>
                <a-col flex="1 1 150px"><NodeAlertsSummary /></a-col>
            </a-row>
        </a-space>
    </a-spin>
</template>
