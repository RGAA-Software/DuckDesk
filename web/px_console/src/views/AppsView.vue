<script setup lang="ts">
import ApplicationCatalogCard from "@/views/apps/ApplicationCatalogCard.vue";
import SchedulingPreviewCard from "@/views/apps/SchedulingPreviewCard.vue";
import { useI18n } from "vue-i18n";
const { t } = useI18n();
</script>

<template>
    <a-tabs default-active-key="catalog">
        <a-tab-pane key="catalog" :tab="t('applications.catalogTitle')">
            <ApplicationCatalogCard />
        </a-tab-pane>
        <a-tab-pane key="diagnostics" :tab="t('scheduling.tabTitle')">
            <SchedulingPreviewCard />
        </a-tab-pane>
    </a-tabs>
</template>
