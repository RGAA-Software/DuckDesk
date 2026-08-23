<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { getSummary, type ResourceSummary } from './api'
const summary = ref<ResourceSummary>()
let timer = 0
let refreshing = false
async function refresh() {
  if (refreshing) return
  refreshing = true
  try { summary.value = await getSummary() } finally { refreshing = false }
}
onMounted(() => { void refresh(); timer = window.setInterval(() => void refresh(), 10000) })
onUnmounted(() => window.clearInterval(timer))
</script>
<template>
  <a-row :gutter="20">
    <a-col :xs="24" :md="8" class="mb-5"><a-card title="远程桌面"><a-statistic :value="summary?.device_count ?? 0" suffix="台" /></a-card></a-col>
    <a-col :xs="24" :md="8" class="mb-5"><a-card title="可用应用"><a-statistic :value="summary?.application_count ?? 0" suffix="个" /></a-card></a-col>
    <a-col :xs="24" :md="8" class="mb-5"><a-card title="活动实例"><a-statistic :value="summary?.active_instance_count ?? 0" suffix="个" /></a-card></a-col>
  </a-row>
</template>
