<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { queryAllServiceConn, queryAllPanelConn, queryRemoteSessionEvents, queryRemoteSessions } from '@/model/conn_api.ts'
import type { ServiceConn, ServiceAuthInfo } from '@/entity/service_conn.ts'
import type { PanelConn } from '@/entity/panel_conn.ts'
import type { RemoteSession, RemoteSessionEvent } from '@/entity/remote_session.ts'
import { formatTimestamp } from '@/util/time.ts'

const POLL_INTERVAL_MS = 10000

const activeTab = ref('service')
const serviceConns = ref<ServiceConn[]>([])
const panelConns = ref<PanelConn[]>([])
const serviceLoading = ref(false)
const panelLoading = ref(false)
const remoteSessionDrawerOpen = ref(false)
const remoteSessionLoading = ref(false)
const selectedDeviceId = ref('')
const remoteSessions = ref<RemoteSession[]>([])
const remoteSessionEvents = ref<RemoteSessionEvent[]>([])

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

function remoteSessionSummary(raw?: string): string {
  try {
    const sessions = JSON.parse(raw || '[]') as Array<{ role?: string }>
    const controllers = sessions.filter((item) => item.role === 'controller').length
    const observers = sessions.filter((item) => item.role === 'observer').length
    return `主控 ${controllers} / 观看 ${observers}`
  } catch {
    return '-'
  }
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

function displayList(values: string[]): string {
  return values.length > 0 ? values.join(' / ') : '-'
}

function sessionEntry(subjectId: string): string {
  return subjectId.startsWith('direct:') ? '直接连接' : 'Console'
}

async function showRemoteSessionDetails(deviceId: string) {
  selectedDeviceId.value = deviceId
  remoteSessionDrawerOpen.value = true
  remoteSessionLoading.value = true
  try {
    const [sessions, events] = await Promise.all([
      queryRemoteSessions(deviceId),
      queryRemoteSessionEvents(deviceId),
    ])
    if (sessions === null || events === null) {
      message.error('查询远控会话审计失败')
      return
    }
    remoteSessions.value = sessions
    remoteSessionEvents.value = events
  } finally {
    remoteSessionLoading.value = false
  }
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

          <a-table-column title="远控会话" min-width="110">
            <template #default="{ record }">
              <span>{{ remoteSessionSummary(record.logical_sessions_json) }}</span>
            </template>
          </a-table-column>

          <a-table-column title="最后心跳" min-width="100">
            <template #default="{ record }">
              <span>{{ formatTs(record.last_update_timestamp) }}</span>
            </template>
          </a-table-column>

          <a-table-column title="操作" min-width="70">
            <template #default="{ record }">
              <a-button type="link" size="small" @click="showRemoteSessionDetails(record.device_id)">详情</a-button>
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

    <a-drawer v-model:open="remoteSessionDrawerOpen" :title="`远控会话：${selectedDeviceId}`" width="780">
      <a-spin :spinning="remoteSessionLoading">
        <h4>当前与历史会话</h4>
        <a-table :data-source="remoteSessions" row-key="logical_session_id" :pagination="false" size="small">
          <a-table-column title="主体" data-index="subject_id" />
          <a-table-column title="入口">
            <template #default="{ record }">
              <a-tag :color="sessionEntry(record.subject_id) === '直接连接' ? 'orange' : 'blue'">
                {{ sessionEntry(record.subject_id) }}
              </a-tag>
            </template>
          </a-table-column>
          <a-table-column title="角色" data-index="role" />
          <a-table-column title="状态">
            <template #default="{ record }"><a-tag :color="record.active ? 'success' : 'default'">{{ record.active ? '进行中' : '已结束' }}</a-tag></template>
          </a-table-column>
          <a-table-column title="传输">
            <template #default="{ record }">{{ displayList(record.transports) }}</template>
          </a-table-column>
          <a-table-column title="接管自" ellipsis>
            <template #default="{ record }">{{ record.takeover_previous_session_id || '-' }}</template>
          </a-table-column>
          <a-table-column title="更新时间">
            <template #default="{ record }">{{ formatTs(record.updated_timestamp) }}</template>
          </a-table-column>
        </a-table>

        <h4 class="mt-5">永久审计记录（最近 500 条）</h4>
        <a-table :data-source="remoteSessionEvents" row-key="event_id" :pagination="false" size="small">
          <a-table-column title="时间">
            <template #default="{ record }">{{ formatTs(record.timestamp) }}</template>
          </a-table-column>
          <a-table-column title="事件" data-index="event_type" />
          <a-table-column title="会话" data-index="logical_session_id" ellipsis />
          <a-table-column title="角色变化">
            <template #default="{ record }">{{ record.previous_role || '-' }} → {{ record.role || '-' }}</template>
          </a-table-column>
          <a-table-column title="相关会话" data-index="related_session_id" ellipsis />
        </a-table>
      </a-spin>
    </a-drawer>

    <div class="h-5" />
  </div>
</template>

<style scoped></style>
