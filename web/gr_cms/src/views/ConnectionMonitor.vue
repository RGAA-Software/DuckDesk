<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { queryAllServiceConn, queryAllPanelConn } from '@/model/conn_api.ts'
import type { ServiceConn, ServiceAuthInfo } from '@/entity/service_conn.ts'
import type { PanelConn } from '@/entity/panel_conn.ts'
import { formatTimestamp } from '@/util/time.ts'

const POLL_INTERVAL_MS = 10000

const activeTab = ref('service')
const serviceConns = ref<ServiceConn[]>([])
const panelConns = ref<PanelConn[]>([])
const serviceLoading = ref(false)
const panelLoading = ref(false)

let pollTimer: number | undefined

// parse auth_info_json, return null when empty or invalid
function parseAuthInfo(json: string): ServiceAuthInfo | null {
  if (!json) {
    return null
  }
  try {
    return JSON.parse(json) as ServiceAuthInfo
  } catch (e) {
    console.error('parse auth_info_json failed', e)
    return null
  }
}

function authInfoText(json: string): string {
  const info = parseAuthInfo(json)
  if (!info) {
    return '-'
  }
  const parts: string[] = []
  if (info.auth_name) {
    parts.push(info.auth_name)
  }
  if (info.end_timestamp_ms) {
    const remainDays = Math.max(
      0,
      Math.ceil((info.end_timestamp_ms - Date.now()) / (24 * 60 * 60 * 1000)),
    )
    parts.push(`剩余 ${remainDays} 天`)
    parts.push(`到期 ${formatTimestamp(info.end_timestamp_ms)}`)
  } else if (info.days !== undefined) {
    parts.push(`剩余 ${info.days} 天`)
  }
  return parts.length > 0 ? parts.join(' / ') : '-'
}

function formatTs(ts?: number): string {
  if (!ts) {
    return '-'
  }
  return formatTimestamp(ts)
}

async function refreshServiceConns() {
  serviceLoading.value = true
  try {
    const conns = await queryAllServiceConn()
    if (conns === null) {
      message.error('查询 Service 连接失败')
      return
    }
    serviceConns.value = conns
  } finally {
    serviceLoading.value = false
  }
}

async function refreshPanelConns() {
  panelLoading.value = true
  try {
    const conns = await queryAllPanelConn()
    if (conns === null) {
      message.error('查询 Panel 连接失败')
      return
    }
    panelConns.value = conns
  } finally {
    panelLoading.value = false
  }
}

async function refreshAll() {
  await Promise.all([refreshServiceConns(), refreshPanelConns()])
}

onMounted(async () => {
  await refreshAll()
  pollTimer = window.setInterval(refreshAll, POLL_INTERVAL_MS)
})

onUnmounted(() => {
  if (pollTimer !== undefined) {
    window.clearInterval(pollTimer)
  }
})
</script>

<template>
  <div>
    <a-tabs v-model:active-key="activeTab">
      <a-tab-pane key="service" tab="Service 连接">
        <a-table :loading="serviceLoading" :data-source="serviceConns" row-key="device_id" style="width: 100%">
          <template #emptyText><span>暂无 Service 连接</span></template>
          <a-table-column title="设备ID" min-width="120">
            <template #default="{ record }">
              <span>{{ record.device_id }}</span>
            </template>
          </a-table-column>

          <a-table-column title="版本" min-width="60">
            <template #default="{ record }">
              <span>{{ record.version || '-' }}</span>
            </template>
          </a-table-column>

          <a-table-column title="render 状态" min-width="60">
            <template #default="{ record }">
              <a-tag :color="record.render_alive ? 'success' : 'default'">
                {{ record.render_alive ? '在线' : '离线' }}
              </a-tag>
            </template>
          </a-table-column>

          <a-table-column title="授权信息" min-width="140">
            <template #default="{ record }">
              <span>{{ authInfoText(record.auth_info_json) }}</span>
            </template>
          </a-table-column>

          <a-table-column title="最后心跳" min-width="100">
            <template #default="{ record }">
              <span>{{ formatTs(record.last_update_timestamp) }}</span>
            </template>
          </a-table-column>
        </a-table>
      </a-tab-pane>

      <a-tab-pane key="panel" tab="Panel 连接">
        <a-table :loading="panelLoading" :data-source="panelConns" row-key="device_id" style="width: 100%">
          <template #emptyText><span>暂无 Panel 连接</span></template>
          <a-table-column title="设备ID" min-width="120">
            <template #default="{ record }">
              <span>{{ record.device_id }}</span>
            </template>
          </a-table-column>

          <a-table-column title="设备名" min-width="80">
            <template #default="{ record }">
              <span>{{ record.device_name || '-' }}</span>
            </template>
          </a-table-column>

          <a-table-column title="IP" min-width="80">
            <template #default="{ record }">
              <span>{{ record.device_ip_addr || '-' }}</span>
            </template>
          </a-table-column>

          <a-table-column title="用户ID" min-width="80">
            <template #default="{ record }">
              <span>{{ record.user_id || '-' }}</span>
            </template>
          </a-table-column>

          <a-table-column title="最后心跳" min-width="100">
            <template #default="{ record }">
              <span>{{ formatTs(record.last_update_timestamp) }}</span>
            </template>
          </a-table-column>
        </a-table>
      </a-tab-pane>
    </a-tabs>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
