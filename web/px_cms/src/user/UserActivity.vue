<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { message, Modal } from 'ant-design-vue'
import { getInstancesPage, openInstance, stopInstance, type InstanceView } from './api'
const instances = ref<InstanceView[]>([])
const loading = ref(false)
const page = ref(1), pageSize = ref(10), total = ref(0)
const keyword = ref(''), state = ref('')
let refreshing = false
let timer = 0
async function refresh(showLoading = false) {
  if (refreshing) return
  refreshing = true
  if (showLoading) loading.value = true
  try {
    const result = await getInstancesPage(page.value, pageSize.value, keyword.value, state.value)
    instances.value = result.items
    total.value = result.total
  }
  catch { if (showLoading) message.error('活动列表加载失败') }
  finally { refreshing = false; loading.value = false }
}
function stop(item: InstanceView) { Modal.confirm({ title: `停止「${item.app_name}」？`, content: '确认后远端实例将退出。', okType: 'danger', async onOk() { try { await stopInstance(item.instance_id); await refresh() } catch { message.error('停止失败') } } }) }
function search() { page.value = 1; void refresh(true) }
function tableChange(p: { current?: number; pageSize?: number }) { page.value = p.current || 1; pageSize.value = p.pageSize || 10; void refresh(true) }
function formatTime(value?: number) { return value ? new Date(value).toLocaleString() : '—' }
onMounted(() => { void refresh(true); timer = window.setInterval(() => void refresh(), 5000) })
onUnmounted(() => window.clearInterval(timer))
</script>
<template>
  <a-card title="我的实例与活动">
    <template #extra><a-space><a-input-search v-model:value="keyword" allow-clear placeholder="应用或实例" @search="search" /><a-select v-model:value="state" style="width: 120px" :options="[{label:'全部状态',value:''},{label:'启动中',value:'starting'},{label:'运行中',value:'running'},{label:'停止中',value:'stopping'},{label:'已停止',value:'stopped'},{label:'失败',value:'failed'}]" @change="search"/><a-button :loading="loading" @click="refresh(true)">刷新</a-button></a-space></template>
    <a-table :data-source="instances" :loading="loading" row-key="instance_id" :pagination="{ current: page, pageSize, total, showSizeChanger: true, showTotal: (value: number) => `共 ${value} 条` }" :scroll="{ x: 1100 }" @change="tableChange">
      <a-table-column title="应用" data-index="app_name" />
      <a-table-column title="状态" data-index="state" />
      <a-table-column title="创建时间" width="180"><template #default="{ record }">{{ formatTime(record.created_at) }}</template></a-table-column>
      <a-table-column title="启动时间" width="180"><template #default="{ record }">{{ formatTime(record.started_at) }}</template></a-table-column>
      <a-table-column title="停止时间" width="180"><template #default="{ record }">{{ formatTime(record.stopped_at) }}</template></a-table-column>
      <a-table-column title="结果" width="220"><template #default="{ record }">{{ record.error_code || '—' }}</template></a-table-column>
      <a-table-column title="操作"><template #default="{ record }">
        <a-space><a-button v-if="record.reconnectable" @click="openInstance(record, undefined, true)">仅观看</a-button><a-button v-if="record.reconnectable" @click="openInstance(record)">进入</a-button><a-button danger :disabled="['stopped','failed'].includes(record.state)" @click="stop(record)">停止</a-button></a-space>
      </template></a-table-column>
    </a-table>
  </a-card>
</template>
