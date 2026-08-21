<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { getInstances, openInstance, stopInstance, type InstanceView } from './api'
const instances = ref<InstanceView[]>([])
const loading = ref(false)
let refreshing = false
let timer = 0
async function refresh(showLoading = false) {
  if (refreshing) return
  refreshing = true
  if (showLoading) loading.value = true
  try { instances.value = await getInstances() }
  catch { if (showLoading) message.error('活动列表加载失败') }
  finally { refreshing = false; loading.value = false }
}
async function stop(item: InstanceView) { try { await stopInstance(item.instance_id); await refresh() } catch { message.error('停止失败') } }
onMounted(() => { void refresh(true); timer = window.setInterval(() => void refresh(), 5000) })
onUnmounted(() => window.clearInterval(timer))
</script>
<template>
  <a-card title="我的实例与活动">
    <template #extra><a-button :loading="loading" @click="refresh(true)">刷新</a-button></template>
    <a-table :data-source="instances" :loading="loading" row-key="instance_id" :pagination="{ pageSize: 10, showSizeChanger: true, showTotal: (total: number) => `共 ${total} 条` }" :scroll="{ x: 760 }">
      <a-table-column title="应用" data-index="app_name" />
      <a-table-column title="状态" data-index="state" />
      <a-table-column title="创建时间"><template #default="{ record }">{{ new Date(record.created_at).toLocaleString() }}</template></a-table-column>
      <a-table-column title="操作"><template #default="{ record }">
        <a-space><a-button v-if="record.reconnectable" @click="openInstance(record)">进入</a-button><a-button danger :disabled="['stopped','failed'].includes(record.state)" @click="stop(record)">停止</a-button></a-space>
      </template></a-table-column>
    </a-table>
  </a-card>
</template>
