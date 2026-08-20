<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getSummary, type ResourceSummary } from './api'
const summary = ref<ResourceSummary>()
onMounted(async () => (summary.value = await getSummary()))
</script>
<template>
  <a-row :gutter="20">
    <a-col :span="8"><a-card title="远程桌面"><a-statistic :value="summary?.device_count ?? 0" suffix="台" /></a-card></a-col>
    <a-col :span="8"><a-card title="可用应用"><a-statistic :value="summary?.application_count ?? 0" suffix="个" /></a-card></a-col>
    <a-col :span="8"><a-card title="活动实例"><a-statistic :value="summary?.active_instance_count ?? 0" suffix="个" /></a-card></a-col>
  </a-row>
</template>
