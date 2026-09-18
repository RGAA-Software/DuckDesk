<script setup lang="ts">
import ApplicationCatalogCard from "@/views/apps/ApplicationCatalogCard.vue";
import DeploymentInventoryCard from "@/views/apps/DeploymentInventoryCard.vue";
import NodeInventoryCard from "@/views/apps/NodeInventoryCard.vue";
</script>

<template>
    <a-space direction="vertical" size="large" class="w-full">
        <ApplicationCatalogCard />
        <NodeInventoryCard />
        <DeploymentInventoryCard />
    </a-space>
</template>
