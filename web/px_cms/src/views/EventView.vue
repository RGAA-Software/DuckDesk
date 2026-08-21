<script setup lang="ts">
import type { CmsEvent } from '@/entity/cms_event.ts'
import axiosHttp from '@/http.ts'
import { copyText } from '@/util/clipboard.ts'
import {
  DatabaseOutlined,
  FundOutlined,
  HddOutlined,
  ReloadOutlined,
  SearchOutlined,
  ThunderboltOutlined,
} from '@ant-design/icons-vue'
import { notification } from 'ant-design-vue'
import { computed, onMounted, reactive, ref } from 'vue'

type EventType = 'cpu' | 'memory' | 'disk' | 'gpu'

interface EventState {
  items: CmsEvent[]
  page: number
  pageSize: number
  total: number
  loading: boolean
}

const eventTypes: Array<{
  key: EventType
  label: string
  shortLabel: string
  description: string
  color: string
}> = [
  { key: 'cpu', label: 'CPU 告警', shortLabel: 'CPU', description: '处理器使用率超过 80%', color: '#1677ff' },
  { key: 'memory', label: '内存告警', shortLabel: '内存', description: '内存使用率超过 80%', color: '#7c3aed' },
  { key: 'disk', label: '磁盘告警', shortLabel: '磁盘', description: '磁盘使用率超过 90%', color: '#ea580c' },
  { key: 'gpu', label: 'GPU 告警', shortLabel: 'GPU', description: '显卡使用率超过 80%', color: '#0891b2' },
]

const states = reactive<Record<EventType, EventState>>({
  cpu: { items: [], page: 1, pageSize: 20, total: 0, loading: false },
  memory: { items: [], page: 1, pageSize: 20, total: 0, loading: false },
  disk: { items: [], page: 1, pageSize: 20, total: 0, loading: false },
  gpu: { items: [], page: 1, pageSize: 20, total: 0, loading: false },
})

const activeType = ref<EventType>('cpu')
const searchDeviceId = ref('')
const searchDeviceName = ref('')
const searchDeviceIp = ref('')
const selectedEvent = ref<CmsEvent | null>(null)
const detailOpen = ref(false)

const currentState = computed(() => states[activeType.value])
const totalRecords = computed(() => eventTypes.reduce((sum, item) => sum + states[item.key].total, 0))

function currentFilters() {
  return {
    device_id: searchDeviceId.value.trim(),
    device_name: searchDeviceName.value.trim(),
    device_ip: searchDeviceIp.value.trim(),
  }
}

async function queryEvents(type: EventType) {
  const state = states[type]
  state.loading = true
  try {
    const response = await axiosHttp.get('/api/v1/event/control/query', {
      params: {
        page: state.page,
        page_size: state.pageSize,
        event_type: type,
        ...currentFilters(),
      },
    })
    if (response.status !== 200 || response.data?.code !== 200) {
      throw new Error(response.data?.message || `HTTP ${response.status}`)
    }
    const items = Array.isArray(response.data.data) ? (response.data.data as CmsEvent[]) : []
    if (items.length === 0 && state.page > 1) {
      state.page = 1
      await queryEvents(type)
      return
    }
    state.items = items
    state.total = items.length > 0 ? Number(items[0]?.total || 0) : 0
  } catch (error) {
    state.items = []
    state.total = 0
    console.error(`query ${type} events failed`, error)
    notification.error({ message: '事件加载失败', description: error instanceof Error ? error.message : String(error) })
  } finally {
    state.loading = false
  }
}

async function queryAllEvents() {
  await Promise.all(eventTypes.map((item) => queryEvents(item.key)))
}

async function handleSearch() {
  for (const item of eventTypes) states[item.key].page = 1
  await queryAllEvents()
}

async function handleReset() {
  searchDeviceId.value = ''
  searchDeviceName.value = ''
  searchDeviceIp.value = ''
  await handleSearch()
}

async function handlePageChange(page: number, pageSize: number) {
  const state = currentState.value
  state.page = pageSize === state.pageSize ? page : 1
  state.pageSize = pageSize
  await queryEvents(activeType.value)
}

function usageOf(event: CmsEvent) {
  if (event.event_type === 'cpu') return event.cpu_usage
  if (event.event_type === 'memory') return event.mem_usage
  if (event.event_type === 'disk') return event.disk_usage
  return event.gpu_usage
}

function usageColor(value: number) {
  if (value >= 95) return 'error'
  if (value >= 90) return 'warning'
  return 'gold'
}

function typeLabel(type: string) {
  return eventTypes.find((item) => item.key === type)?.shortLabel || type || '-'
}

function resourceOf(event: CmsEvent) {
  if (event.event_type === 'disk') return event.disk_path || '未知磁盘'
  if (event.event_type === 'gpu') return event.gpu_name || event.gpu_id || '未知显卡'
  if (event.event_type === 'memory') return '物理内存'
  return '处理器'
}

function occurrenceCount(event: CmsEvent) {
  return Math.max(1, Number(event.occurrence_count || 0))
}

function firstReportedAt(event: CmsEvent) {
  return event.first_readable_timestamp || event.readable_timestamp || '-'
}

function openDetail(event: CmsEvent) {
  selectedEvent.value = event
  detailOpen.value = true
}

async function handleCopyEvent(event: CmsEvent) {
  await copyText(JSON.stringify(event, null, 2))
  notification.success({ message: '事件信息已复制' })
}

onMounted(queryAllEvents)
</script>

<template>
  <section class="event-page">
    <header class="page-header">
      <div>
        <h2>上报事件</h2>
        <p>资源异常事件按设备和资源归档；相同占用率重复上报时刷新最近时间，不重复生成记录。</p>
      </div>
      <a-button :loading="eventTypes.some((item) => states[item.key].loading)" @click="queryAllEvents">
        <template #icon><ReloadOutlined /></template>
        刷新
      </a-button>
    </header>

    <a-card class="filter-card" :bordered="false">
      <div class="filter-grid">
        <label>
          <span>设备 ID</span>
          <a-input v-model:value="searchDeviceId" allow-clear placeholder="精确或部分设备 ID" @press-enter="handleSearch" />
        </label>
        <label>
          <span>设备名称</span>
          <a-input v-model:value="searchDeviceName" allow-clear placeholder="输入设备名称" @press-enter="handleSearch" />
        </label>
        <label>
          <span>设备 IP</span>
          <a-input v-model:value="searchDeviceIp" allow-clear placeholder="输入 IP 地址" @press-enter="handleSearch" />
        </label>
        <div class="filter-actions">
          <a-button type="primary" @click="handleSearch">
            <template #icon><SearchOutlined /></template>
            查询
          </a-button>
          <a-button @click="handleReset">重置</a-button>
        </div>
      </div>
    </a-card>

    <div class="summary-grid">
      <button
        v-for="item in eventTypes"
        :key="item.key"
        class="summary-card"
        :class="{ active: activeType === item.key }"
        :style="{ '--accent': item.color }"
        type="button"
        @click="activeType = item.key"
      >
        <span class="summary-label">{{ item.label }}</span>
        <strong>{{ states[item.key].total }}</strong>
        <small>{{ item.description }} · 状态记录</small>
      </button>
    </div>

    <a-card class="event-card" :bordered="false">
      <div class="table-heading">
        <div>
          <strong>事件记录</strong>
          <span>当前筛选共 {{ totalRecords }} 条状态，{{ typeLabel(activeType) }} {{ currentState.total }} 条</span>
        </div>
        <span class="stat-note">“累计上报”表示该行合并了多少次相同状态</span>
      </div>

      <a-tabs v-model:active-key="activeType" class="event-tabs">
        <a-tab-pane key="cpu"><template #tab><span><FundOutlined /> CPU</span></template></a-tab-pane>
        <a-tab-pane key="memory"><template #tab><span><DatabaseOutlined /> 内存</span></template></a-tab-pane>
        <a-tab-pane key="disk"><template #tab><span><HddOutlined /> 磁盘</span></template></a-tab-pane>
        <a-tab-pane key="gpu"><template #tab><span><ThunderboltOutlined /> GPU</span></template></a-tab-pane>
      </a-tabs>

      <a-table
        :data-source="currentState.items"
        :loading="currentState.loading"
        :pagination="false"
        :scroll="{ x: 1380 }"
        row-key="event_id"
        size="middle"
        table-layout="fixed"
      >
        <a-table-column title="时间" :width="220" fixed="left">
          <template #default="{ record }">
            <div class="time-cell">
              <span><em>最近</em>{{ record.readable_timestamp || '-' }}</span>
              <span v-if="occurrenceCount(record) > 1"><em>首次</em>{{ firstReportedAt(record) }}</span>
            </div>
          </template>
        </a-table-column>
        <a-table-column title="类型" :width="90">
          <template #default="{ record }"><a-tag>{{ typeLabel(record.event_type) }}</a-tag></template>
        </a-table-column>
        <a-table-column title="资源" :width="160" ellipsis>
          <template #default="{ record }">
            <a-tooltip :title="resourceOf(record)"><span class="resource-name">{{ resourceOf(record) }}</span></a-tooltip>
          </template>
        </a-table-column>
        <a-table-column title="使用率" :width="100" align="center">
          <template #default="{ record }"><a-tag :color="usageColor(usageOf(record))" class="usage-tag">{{ usageOf(record) }}%</a-tag></template>
        </a-table-column>
        <a-table-column title="累计上报" :width="100" align="center">
          <template #default="{ record }"><strong>{{ occurrenceCount(record) }}</strong></template>
        </a-table-column>
        <a-table-column title="设备" :width="220" ellipsis>
          <template #default="{ record }">
            <div class="device-cell">
              <a-tooltip :title="record.device_name || '未命名设备'"><strong>{{ record.device_name || '未命名设备' }}</strong></a-tooltip>
              <a-tooltip :title="record.device_id"><span>{{ record.device_id || '-' }}</span></a-tooltip>
            </div>
          </template>
        </a-table-column>
        <a-table-column title="设备 IP" data-index="device_ip" :width="145" ellipsis />
        <a-table-column title="上报用户" :width="160" ellipsis>
          <template #default="{ record }">
            <a-tooltip :title="record.user_id || ''"><span>{{ record.user_name || record.user_id || '系统' }}</span></a-tooltip>
          </template>
        </a-table-column>
        <a-table-column title="事件 ID" :width="210" ellipsis>
          <template #default="{ record }"><a-tooltip :title="record.event_id"><code>{{ record.event_id }}</code></a-tooltip></template>
        </a-table-column>
        <a-table-column title="操作" :width="125" fixed="right">
          <template #default="{ record }">
            <a-space size="small">
              <a-button type="link" size="small" @click="openDetail(record)">详情</a-button>
              <a-button type="link" size="small" @click="handleCopyEvent(record)">复制</a-button>
            </a-space>
          </template>
        </a-table-column>
      </a-table>

      <div class="pagination-row">
        <span>第 {{ currentState.page }} 页，共 {{ currentState.total }} 条</span>
        <a-pagination
          :current="currentState.page"
          :page-size="currentState.pageSize"
          :page-size-options="['20', '40', '60', '80']"
          :total="currentState.total"
          show-size-changer
          show-quick-jumper
          @change="handlePageChange"
        />
      </div>
    </a-card>

    <a-drawer v-model:open="detailOpen" title="事件详情" width="560">
      <a-descriptions v-if="selectedEvent" :column="1" bordered size="small">
        <a-descriptions-item label="事件 ID">{{ selectedEvent.event_id }}</a-descriptions-item>
        <a-descriptions-item label="事件类型">{{ typeLabel(selectedEvent.event_type) }}</a-descriptions-item>
        <a-descriptions-item label="资源">{{ resourceOf(selectedEvent) }}</a-descriptions-item>
        <a-descriptions-item label="使用率">{{ usageOf(selectedEvent) }}%</a-descriptions-item>
        <a-descriptions-item label="累计上报">{{ occurrenceCount(selectedEvent) }} 次</a-descriptions-item>
        <a-descriptions-item label="首次上报">{{ firstReportedAt(selectedEvent) }}</a-descriptions-item>
        <a-descriptions-item label="最近上报">{{ selectedEvent.readable_timestamp || '-' }}</a-descriptions-item>
        <a-descriptions-item label="设备名称">{{ selectedEvent.device_name || '-' }}</a-descriptions-item>
        <a-descriptions-item label="设备 ID">{{ selectedEvent.device_id || '-' }}</a-descriptions-item>
        <a-descriptions-item label="设备 IP">{{ selectedEvent.device_ip || '-' }}</a-descriptions-item>
        <a-descriptions-item label="用户名">{{ selectedEvent.user_name || '系统' }}</a-descriptions-item>
        <a-descriptions-item label="用户 ID">{{ selectedEvent.user_id || '-' }}</a-descriptions-item>
      </a-descriptions>
      <template #extra><a-button v-if="selectedEvent" @click="handleCopyEvent(selectedEvent)">复制 JSON</a-button></template>
    </a-drawer>
  </section>
</template>

<style scoped>
.event-page { display: flex; flex-direction: column; gap: 12px; min-width: 0; color: #344054; }
.page-header { display: flex; align-items: center; justify-content: space-between; gap: 20px; padding: 4px 2px; }
.page-header h2 { margin: 0; color: #182230; font-size: 22px; line-height: 32px; }
.page-header p { margin: 4px 0 0; color: #667085; font-size: 13px; }
.filter-card, .event-card { box-shadow: 0 1px 3px rgba(16, 24, 40, .06); }
.filter-card :deep(.ant-card-body) { padding: 16px 18px; }
.filter-grid { display: grid; grid-template-columns: repeat(3, minmax(180px, 1fr)) auto; align-items: end; gap: 14px; }
.filter-grid label { display: flex; flex-direction: column; gap: 6px; min-width: 0; color: #475467; font-size: 13px; }
.filter-actions { display: flex; gap: 8px; padding-bottom: 1px; }
.summary-grid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 10px; }
.summary-card { position: relative; display: flex; flex-direction: column; align-items: flex-start; min-width: 0; padding: 14px 16px; overflow: hidden; text-align: left; background: #fff; border: 1px solid #e4e7ec; border-radius: 8px; cursor: pointer; transition: border-color .2s, box-shadow .2s, transform .2s; }
.summary-card::before { position: absolute; top: 0; right: 0; left: 0; height: 3px; background: var(--accent); content: ''; opacity: .7; }
.summary-card:hover, .summary-card.active { border-color: var(--accent); box-shadow: 0 3px 10px rgba(16, 24, 40, .08); transform: translateY(-1px); }
.summary-label { color: #475467; font-size: 13px; }
.summary-card strong { margin-top: 5px; color: #101828; font-size: 26px; line-height: 32px; }
.summary-card small { margin-top: 3px; overflow: hidden; color: #98a2b3; font-size: 11px; text-overflow: ellipsis; white-space: nowrap; }
.event-card :deep(.ant-card-body) { padding: 0; }
.table-heading { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 16px 18px 4px; }
.table-heading > div { display: flex; align-items: baseline; gap: 12px; }
.table-heading strong { color: #182230; font-size: 16px; }
.table-heading span, .stat-note { color: #667085; font-size: 12px; }
.event-tabs { padding: 0 18px; }
.event-tabs :deep(.ant-tabs-nav) { margin-bottom: 0; }
.event-tabs :deep(.ant-tabs-tab span) { display: inline-flex; align-items: center; gap: 6px; }
.event-card :deep(.ant-table-wrapper) { border-top: 1px solid #f0f1f3; }
.event-card :deep(.ant-table-cell) { color: #344054; font-size: 13px; }
.time-cell, .device-cell { display: flex; flex-direction: column; min-width: 0; gap: 2px; }
.time-cell span { display: flex; gap: 7px; white-space: nowrap; }
.time-cell em { width: 28px; color: #98a2b3; font-size: 11px; font-style: normal; }
.device-cell strong, .device-cell span, .resource-name { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.device-cell strong { color: #182230; font-weight: 600; }
.device-cell span { color: #667085; font: 11px/17px ui-monospace, SFMono-Regular, Consolas, monospace; }
.usage-tag { min-width: 54px; margin: 0; font-weight: 700; text-align: center; }
code { color: #475467; font-size: 11px; }
.pagination-row { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 14px 18px 18px; color: #667085; font-size: 12px; }
@media (max-width: 1100px) { .summary-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); } .filter-grid { grid-template-columns: repeat(2, minmax(180px, 1fr)); } }
@media (max-width: 700px) { .page-header, .table-heading, .pagination-row { align-items: flex-start; flex-direction: column; } .summary-grid, .filter-grid { grid-template-columns: 1fr; } .filter-actions { width: 100%; } .stat-note { display: none; } }
</style>
