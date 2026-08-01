<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
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
      ElMessage.error('查询 Service 连接失败')
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
      ElMessage.error('查询 Panel 连接失败')
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
    <el-tabs v-model="activeTab">
      <el-tab-pane label="Service 连接" name="service">
        <el-table v-loading="serviceLoading" :data="serviceConns" style="width: 100%">
          <el-table-column label="设备ID" :min-width="120">
            <template #default="scope">
              <span>{{ scope.row.device_id }}</span>
            </template>
          </el-table-column>

          <el-table-column label="版本" :min-width="60">
            <template #default="scope">
              <span>{{ scope.row.version || '-' }}</span>
            </template>
          </el-table-column>

          <el-table-column label="render 状态" :min-width="60">
            <template #default="scope">
              <el-tag :type="scope.row.render_alive ? 'success' : 'info'">
                {{ scope.row.render_alive ? '在线' : '离线' }}
              </el-tag>
            </template>
          </el-table-column>

          <el-table-column label="授权信息" :min-width="140">
            <template #default="scope">
              <span>{{ authInfoText(scope.row.auth_info_json) }}</span>
            </template>
          </el-table-column>

          <el-table-column label="最后心跳" :min-width="100">
            <template #default="scope">
              <span>{{ formatTs(scope.row.last_update_timestamp) }}</span>
            </template>
          </el-table-column>

          <template #empty>
            <span>暂无 Service 连接</span>
          </template>
        </el-table>
      </el-tab-pane>

      <el-tab-pane label="Panel 连接" name="panel">
        <el-table v-loading="panelLoading" :data="panelConns" style="width: 100%">
          <el-table-column label="设备ID" :min-width="120">
            <template #default="scope">
              <span>{{ scope.row.device_id }}</span>
            </template>
          </el-table-column>

          <el-table-column label="设备名" :min-width="80">
            <template #default="scope">
              <span>{{ scope.row.device_name || '-' }}</span>
            </template>
          </el-table-column>

          <el-table-column label="IP" :min-width="80">
            <template #default="scope">
              <span>{{ scope.row.device_ip_addr || '-' }}</span>
            </template>
          </el-table-column>

          <el-table-column label="用户ID" :min-width="80">
            <template #default="scope">
              <span>{{ scope.row.user_id || '-' }}</span>
            </template>
          </el-table-column>

          <el-table-column label="最后心跳" :min-width="100">
            <template #default="scope">
              <span>{{ formatTs(scope.row.last_update_timestamp) }}</span>
            </template>
          </el-table-column>

          <template #empty>
            <span>暂无 Panel 连接</span>
          </template>
        </el-table>
      </el-tab-pane>
    </el-tabs>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
