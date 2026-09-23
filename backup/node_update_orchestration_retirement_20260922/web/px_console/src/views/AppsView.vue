<script setup lang="ts">
import ApplicationCatalogCard from "@/views/apps/ApplicationCatalogCard.vue";
import DeploymentInventoryCard from "@/views/apps/DeploymentInventoryCard.vue";
import NodeInventoryCard from "@/views/apps/NodeInventoryCard.vue";
import SchedulingPreviewCard from "@/views/apps/SchedulingPreviewCard.vue";
import UpdateTrustCard from "@/views/apps/UpdateTrustCard.vue";
</script>

<template>
    <a-space direction="vertical" size="large" class="w-full">
        <ApplicationCatalogCard />
        <NodeInventoryCard />
        <UpdateTrustCard />
        <DeploymentInventoryCard />
        <SchedulingPreviewCard />
    </a-space>
</template>
