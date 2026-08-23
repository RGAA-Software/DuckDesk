<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { getDevicesPage, openDevice, type DeviceSummary } from './api'
const devices = ref<DeviceSummary[]>([])
const loading = ref(false)
const page = ref(1)
const pageSize = 10
const total = ref(0)
const keyword = ref('')
let refreshing = false
let timer = 0
async function refresh(showLoading = false) {
  if (refreshing) return
  refreshing = true
  if (showLoading) loading.value = true
  try {
    const result = await getDevicesPage(page.value, pageSize, keyword.value)
    devices.value = result.items
    total.value = result.total
  }
  catch { if (showLoading) message.error('设备列表加载失败') }
  finally { refreshing = false; loading.value = false }
}
async function connect(device: DeviceSummary, viewOnly = false) { try { await openDevice(device.device_id, viewOnly) } catch { message.error('连接票据签发失败，请确认设备在线或授权仍有效') } }
function search() { page.value = 1; void refresh(true) }
function changePage(value: number) { page.value = value; void refresh(true) }
onMounted(() => { void refresh(true); timer = window.setInterval(() => void refresh(), 10000) })
onUnmounted(() => window.clearInterval(timer))
</script>
<template>
  <a-card title="我的远程桌面">
    <template #extra><a-space><a-input-search v-model:value="keyword" allow-clear placeholder="设备名称或 ID" @search="search" /><a-button @click="refresh(true)" :loading="loading">刷新</a-button></a-space></template>
    <a-empty v-if="!loading && devices.length === 0" description="管理员尚未向你或你的用户组授权设备" />
    <a-list v-else :data-source="devices" :loading="loading" :pagination="{ current: page, pageSize, total, showSizeChanger: false, onChange: changePage }">
      <template #renderItem="{ item }"><a-list-item>
        <a-list-item-meta :title="item.name || item.device_id" :description="`最后上报：${new Date(item.last_seen_at).toLocaleString()}`" />
        <a-tag :color="item.online ? 'green' : 'default'">{{ item.online ? '在线' : '离线' }}</a-tag>
        <a-space class="ml-4"><a-button :disabled="!item.online" @click="connect(item, true)">仅观看</a-button><a-button type="primary" :disabled="!item.online" @click="connect(item)">连接</a-button></a-space>
      </a-list-item></template>
    </a-list>
  </a-card>
</template>
