<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { getDevices, openDevice, type DeviceSummary } from './api'
const devices = ref<DeviceSummary[]>([])
const loading = ref(false)
async function refresh() { loading.value = true; try { devices.value = await getDevices() } finally { loading.value = false } }
async function connect(device: DeviceSummary) { try { await openDevice(device.device_id) } catch { message.error('连接票据签发失败，请确认设备在线') } }
onMounted(refresh)
</script>
<template>
  <a-card title="我的远程桌面">
    <template #extra><a-button @click="refresh" :loading="loading">刷新</a-button></template>
    <a-empty v-if="!loading && devices.length === 0" description="管理员尚未向你或你的用户组授权设备" />
    <a-list v-else :data-source="devices" :loading="loading">
      <template #renderItem="{ item }"><a-list-item>
        <a-list-item-meta :title="item.name || item.device_id" :description="`最后上报：${new Date(item.last_seen_at).toLocaleString()}`" />
        <a-tag :color="item.online ? 'green' : 'default'">{{ item.online ? '在线' : '离线' }}</a-tag>
        <a-button class="ml-4" type="primary" :disabled="!item.online" @click="connect(item)">连接</a-button>
      </a-list-item></template>
    </a-list>
  </a-card>
</template>
