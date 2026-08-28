<script setup lang="ts">
import {
  FolderOutlined,
  LinkOutlined,
  ReloadOutlined,
  SafetyCertificateOutlined,
  SearchOutlined,
} from '@ant-design/icons-vue'
import { onMounted, ref } from 'vue'
import { formatDuration, formatTimestamp } from '@/util/time.ts'
import { connTypeTagType, formatConnTypeLabel } from '@/util/conn_type.ts'
import type { Visit } from '@/entity/visit.ts'
import axiosHttp from '@/http.ts'
import type { FileTransfer } from '@/entity/file_transfer.ts'
import {
  fileTransferReasonLabel,
  fileTransferStatusColor as statusColor,
  fileTransferStatusLabel as statusLabel,
} from '@/util/file_transfer_terminal'
import { copyText } from '@/util/clipboard.ts'
import { notification } from 'ant-design-vue'
import type { ConsoleEvent } from '@/entity/console_event.ts'

const visitDeviceId = ref('')
const targetDeviceId = ref('')

// -------------------------------------Visit-----------------------------------------
const visitPageSize = ref(20)
const visitCurrentPage = ref(1)
const totalVisitSize = ref(0)

// visit histories
const visits = ref<Visit[]>([])

const handleVisitPageChange = (page: number, pageSize: number) => {
  visitCurrentPage.value = page
  visitPageSize.value = pageSize
  queryVisits(page, pageSize, visitDeviceId.value, targetDeviceId.value)
}

// request device
async function queryVisits(
  page: number,
  pageSize: number,
  visitDevice: string,
  targetDevice: string,
) {
  const resp = await axiosHttp.get('/api/v1/record/query_visit_info', {
    params: {
      page: page,
      page_size: pageSize,
      sort_time: -1,
      visit_device_id: visitDevice ? visitDevice.trim() : '',
      target_device_id: targetDevice ? targetDevice.trim() : '',
    },
  })
  if (resp.status !== 200) {
    console.error('queryVisits failed', resp)
    return
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryVisits failed, data:', data)
    return
  }

  visits.value = data.data
  if (visits.value.length > 0 && visits.value[0] !== null) {
    totalVisitSize.value = visits.value[0]!.total
  } else {
    totalVisitSize.value = 0
  }

  //console.log('device list, total: ', totalVisitSize.value, ', devices: ', visits.value)
}

// -------------------------------------File Transfer-----------------------------------------
const fileTransferPageSize = ref(20)
const fileTransferCurrentPage = ref(1)
const totalFileTransferSize = ref(0)
// file transfer histories
const fileTransfers = ref<FileTransfer[]>([])

const handleFileTransferPageChange = (page: number, pageSize: number) => {
  fileTransferCurrentPage.value = page
  fileTransferPageSize.value = pageSize
  queryFileTransfers(page, pageSize, visitDeviceId.value, targetDeviceId.value)
}

// -------------------------------------Management Audit-----------------------------------------
const auditPageSize = ref(20)
const auditCurrentPage = ref(1)
const totalAuditSize = ref(0)
const auditEvents = ref<ConsoleEvent[]>([])
const auditActorId = ref('')
const auditAction = ref('')
const auditTargetId = ref('')
const auditLoading = ref(false)

async function queryAuditEvents() {
  auditLoading.value = true
  try {
    const response = await axiosHttp.get('/api/v1/event/control/query', {
      params: {
        page: auditCurrentPage.value,
        page_size: auditPageSize.value,
        event_type: 'security_audit',
        actor_id: auditActorId.value.trim(),
        action: auditAction.value.trim(),
        target_id: auditTargetId.value.trim(),
      },
    })
    if (response.status !== 200 || response.data?.code !== 200) {
      throw new Error(response.data?.message || `HTTP ${response.status}`)
    }
    auditEvents.value = Array.isArray(response.data.data) ? response.data.data : []
    totalAuditSize.value = Number(auditEvents.value[0]?.total || 0)
  } catch (error) {
    auditEvents.value = []
    totalAuditSize.value = 0
    notification.error({ message: '管理审计加载失败', description: error instanceof Error ? error.message : String(error) })
  } finally {
    auditLoading.value = false
  }
}

async function handleAuditPageChange(page: number, pageSize: number) {
  auditCurrentPage.value = pageSize === auditPageSize.value ? page : 1
  auditPageSize.value = pageSize
  await queryAuditEvents()
}

async function handleAuditSearch() {
  auditCurrentPage.value = 1
  await queryAuditEvents()
}

async function handleAuditClear() {
  auditActorId.value = ''
  auditAction.value = ''
  auditTargetId.value = ''
  await handleAuditSearch()
}

const handleCopyVisitInfo = (index: number, visit: Visit) => {
  console.log(index, visit)
  copyText(JSON.stringify(visit))
  notification.success({
    message: '复制成功',
  })
}

const handleCopyFileTransferInfo = (index: number, ft: FileTransfer) => {
  console.log(index, ft)
  copyText(JSON.stringify(ft))
  notification.success({
    message: '复制成功',
  })
}

const handleCopyAuditInfo = (event: ConsoleEvent) => {
  copyText(JSON.stringify(event, null, 2))
  notification.success({ message: '复制成功' })
}

const totalText = (total: number) => `共 ${total} 条`

// map conn_type util tag types (success/primary/warning/info/danger) to antd tag colors
const connTagColor = (type: string): string => {
  switch (type) {
    case 'success':
      return 'success'
    case 'primary':
      return 'processing'
    case 'warning':
      return 'warning'
    case 'danger':
      return 'error'
    default:
      return 'default'
  }
}

// request file transfer history
async function queryFileTransfers(
  page: number,
  pageSize: number,
  visitDevice: string,
  targetDevice: string,
) {
  const resp = await axiosHttp.get('/api/v1/record/query_file_transfer_info', {
    params: {
      page: page,
      page_size: pageSize,
      sort_time: -1,
      visit_device_id: visitDevice ? visitDevice.trim() : '',
      target_device_id: targetDevice ? targetDevice.trim() : '',
    },
  })
  if (resp.status !== 200) {
    console.error('queryFileTransfers failed', resp)
    return
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('queryFileTransfers failed, data:', data)
    return
  }

  fileTransfers.value = data.data
  if (fileTransfers.value.length > 0 && fileTransfers.value[0] !== null) {
    totalFileTransferSize.value = fileTransfers.value[0]!.total
  } else {
    totalFileTransferSize.value = 0
  }

  //console.log('device list, total: ', totalFileTransferSize.value, ', filetransfer: ', fileTransfers.value)
}

// onMounted
onMounted(async () => {
  await Promise.all([
    queryVisits(visitCurrentPage.value, visitPageSize.value, '', ''),
    queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, '', ''),
    queryAuditEvents(),
  ])
})

// search
const handleSearch = async () => {
  visitCurrentPage.value = 1
  fileTransferCurrentPage.value = 1
  // 1. search records
  await queryVisits(
    visitCurrentPage.value,
    visitPageSize.value,
    visitDeviceId.value,
    targetDeviceId.value,
  )
  // 2. search file transfers
  await queryFileTransfers(
    fileTransferCurrentPage.value,
    fileTransferPageSize.value,
    visitDeviceId.value,
    targetDeviceId.value,
  )
}

const handleClear = async () => {
  visitDeviceId.value = ''
  targetDeviceId.value = ''
  visitCurrentPage.value = 1
  fileTransferCurrentPage.value = 1
  await queryVisits(visitCurrentPage.value, visitPageSize.value, '', '')
  await queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, '', '')
}

const handleRefresh = async () => {
  await Promise.all([
    queryVisits(visitCurrentPage.value, visitPageSize.value, visitDeviceId.value, targetDeviceId.value),
    queryFileTransfers(fileTransferCurrentPage.value, fileTransferPageSize.value, visitDeviceId.value, targetDeviceId.value),
    queryAuditEvents(),
  ])
}
</script>

<template>
  <section class="security-page">
    <header class="page-header">
      <div>
        <h2>安全审计</h2>
        <p>集中查看远程访问、文件传输与后台管理操作，异常终止和系统恢复记录会明确标记。</p>
      </div>
      <a-button @click="handleRefresh">
        <template #icon><ReloadOutlined /></template>
        刷新全部
      </a-button>
    </header>

    <a-card class="filter-card" :bordered="false">
      <div class="filter-grid">
        <label>
          <span>发起设备 ID</span>
          <a-input v-model:value="visitDeviceId" allow-clear placeholder="输入发起设备 ID" @press-enter="handleSearch" />
        </label>
        <label>
          <span>目标设备 ID</span>
          <a-input v-model:value="targetDeviceId" allow-clear placeholder="输入目标设备 ID" @press-enter="handleSearch" />
        </label>
        <div class="filter-actions">
          <a-button type="primary" @click="handleSearch">
            <template #icon><SearchOutlined /></template>
            查询访问与传输
          </a-button>
          <a-button @click="handleClear">重置</a-button>
        </div>
      </div>
    </a-card>

    <a-card class="audit-card" :bordered="false">
      <a-tabs class="audit-tabs">
        <a-tab-pane key="visit">
          <template #tab><span class="tab-label"><LinkOutlined />访问记录</span></template>

          <div class="tab-heading">
            <div><strong>远程访问记录</strong><span>连接建立、结束状态及异常恢复信息</span></div>
            <a-tag color="blue">{{ totalVisitSize }} 条</a-tag>
          </div>

          <a-table
            class="security-table"
            :data-source="visits"
            row-key="conn_id"
            :pagination="false"
            :scroll="{ x: 1360 }"
            size="middle"
            table-layout="fixed"
          >
            <a-table-column title="连接类型" :width="110">
              <template #default="{ record }"><a-tag :color="connTagColor(connTypeTagType(record.conn_type))">{{ formatConnTypeLabel(record.conn_type) }}</a-tag></template>
            </a-table-column>
            <a-table-column title="开始时间" :width="190"><template #default="{ record }">{{ formatTimestamp(record.begin) }}</template></a-table-column>
            <a-table-column title="结束时间" :width="190"><template #default="{ record }">{{ record.end > 0 ? formatTimestamp(record.end) : '-' }}</template></a-table-column>
            <a-table-column title="连接时长" :width="120"><template #default="{ record }">{{ formatDuration(record.duration) }}</template></a-table-column>
            <a-table-column title="状态" :width="110" align="center"><template #default="{ record }"><a-tag :color="statusColor(record)">{{ statusLabel(record) }}</a-tag></template></a-table-column>
            <a-table-column title="结束原因" :width="220" ellipsis>
              <template #default="{ record }"><div class="reason-cell"><span>{{ fileTransferReasonLabel(record.end_reason) }}</span><a-tag v-if="record.recovered" color="warning">恢复补录</a-tag></div></template>
            </a-table-column>
            <a-table-column title="发起设备" :width="180" ellipsis><template #default="{ record }"><strong class="device-id">{{ record.visitor_device }}</strong></template></a-table-column>
            <a-table-column title="目标设备" :width="180" ellipsis><template #default="{ record }"><strong class="device-id">{{ record.target_device }}</strong></template></a-table-column>
            <a-table-column title="操作" :width="90" fixed="right"><template #default="{ record, index }"><a-button type="link" size="small" @click="handleCopyVisitInfo(index, record)">复制</a-button></template></a-table-column>
          </a-table>

          <div class="pagination-shell">
            <span class="footer-total">共 {{ totalVisitSize }} 条记录</span>
            <a-pagination
              v-model:current="visitCurrentPage"
              v-model:page-size="visitPageSize"
              :page-size-options="['20', '40', '60', '80']"
              :show-total="totalText"
              :total="totalVisitSize"
              show-quick-jumper
              show-size-changer
              @change="handleVisitPageChange"
            />
            <span class="footer-spacer" />
          </div>
        </a-tab-pane>

        <a-tab-pane key="file-transfer">
          <template #tab><span class="tab-label"><FolderOutlined />文件传输</span></template>

          <div class="tab-heading">
            <div><strong>文件传输记录</strong><span>传输方向、结果、耗时及异常中断原因</span></div>
            <a-tag color="purple">{{ totalFileTransferSize }} 条</a-tag>
          </div>

          <a-table
            class="security-table"
            :data-source="fileTransfers"
            row-key="the_file_id"
            :pagination="false"
            :scroll="{ x: 1620 }"
            size="middle"
            table-layout="fixed"
          >
            <a-table-column title="文件 ID" :width="160" ellipsis><template #default="{ record }"><a-tooltip :title="record.the_file_id"><code>{{ record.the_file_id }}</code></a-tooltip></template></a-table-column>
            <a-table-column title="发起设备" :width="170" ellipsis><template #default="{ record }"><span class="device-id">{{ record.visitor_device }}</span></template></a-table-column>
            <a-table-column title="目标设备" :width="170" ellipsis><template #default="{ record }"><span class="device-id">{{ record.target_device }}</span></template></a-table-column>
            <a-table-column title="开始时间" :width="190"><template #default="{ record }">{{ formatTimestamp(record.begin) }}</template></a-table-column>
            <a-table-column title="结束时间" :width="190"><template #default="{ record }">{{ record.end > 0 ? formatTimestamp(record.end) : '-' }}</template></a-table-column>
            <a-table-column title="状态" :width="110" align="center"><template #default="{ record }"><a-tag :color="statusColor(record)">{{ statusLabel(record) }}</a-tag></template></a-table-column>
            <a-table-column title="结束原因" :width="210" ellipsis><template #default="{ record }"><div class="reason-cell"><span>{{ fileTransferReasonLabel(record.end_reason) }}</span><a-tag v-if="record.recovered" color="warning">恢复补录</a-tag></div></template></a-table-column>
            <a-table-column title="耗时" :width="110"><template #default="{ record }">{{ record.duration > 0 ? formatDuration(record.duration) : record.end > 0 && record.begin > 0 ? formatDuration(record.end - record.begin) : '-' }}</template></a-table-column>
            <a-table-column title="方向" :width="90" align="center"><template #default="{ record }"><a-tag :color="record.direction === 'In' ? 'success' : 'processing'">{{ record.direction === 'In' ? '传入' : '传出' }}</a-tag></template></a-table-column>
            <a-table-column title="文件路径" :width="220" ellipsis><template #default="{ record }"><a-tooltip :title="record.file_detail"><span>{{ record.file_detail || '-' }}</span></a-tooltip></template></a-table-column>
            <a-table-column title="操作" :width="90" fixed="right"><template #default="{ record, index }"><a-button type="link" size="small" @click="handleCopyFileTransferInfo(index, record)">复制</a-button></template></a-table-column>
          </a-table>

          <div class="pagination-shell">
            <span class="footer-total">共 {{ totalFileTransferSize }} 条记录</span>
            <a-pagination
              v-model:current="fileTransferCurrentPage"
              v-model:page-size="fileTransferPageSize"
              :page-size-options="['20', '40', '60', '80']"
              :show-total="totalText"
              :total="totalFileTransferSize"
              show-quick-jumper
              show-size-changer
              @change="handleFileTransferPageChange"
            />
            <span class="footer-spacer" />
          </div>
        </a-tab-pane>

        <a-tab-pane key="management-audit">
          <template #tab><span class="tab-label"><SafetyCertificateOutlined />管理操作</span></template>

          <div class="tab-heading management-heading">
            <div><strong>后台管理审计</strong><span>按真实管理员身份追踪资源变更与操作结果</span></div>
            <a-tag color="gold">{{ totalAuditSize }} 条</a-tag>
          </div>

          <div class="management-filter">
            <a-input v-model:value="auditActorId" allow-clear placeholder="操作人 ID" @press-enter="handleAuditSearch" />
            <a-input v-model:value="auditAction" allow-clear placeholder="操作类型，如 user_delete" @press-enter="handleAuditSearch" />
            <a-input v-model:value="auditTargetId" allow-clear placeholder="目标对象 ID" @press-enter="handleAuditSearch" />
            <div class="management-actions"><a-button type="primary" @click="handleAuditSearch">查询</a-button><a-button @click="handleAuditClear">重置</a-button></div>
          </div>

          <a-table
            class="security-table"
            :data-source="auditEvents"
            row-key="event_id"
            :pagination="false"
            :loading="auditLoading"
            :scroll="{ x: 1420 }"
            size="middle"
            table-layout="fixed"
          >
            <a-table-column title="发生时间" :width="200"><template #default="{ record }">{{ record.readable_timestamp || formatTimestamp(record.timestamp) }}</template></a-table-column>
            <a-table-column title="操作人" :width="220"><template #default="{ record }"><strong>{{ record.actor_id || '-' }}</strong><div class="secondary">{{ record.actor_type || '-' }}</div></template></a-table-column>
            <a-table-column title="操作" data-index="action" :width="220" ellipsis />
            <a-table-column title="结果" :width="100" align="center"><template #default="{ record }"><a-tag :color="record.result === 'success' ? 'success' : 'error'">{{ record.result || '-' }}</a-tag></template></a-table-column>
            <a-table-column title="目标对象" :width="260" ellipsis><template #default="{ record }"><span>{{ record.target_id || '-' }}</span><div class="secondary">{{ record.target_type || '-' }}</div></template></a-table-column>
            <a-table-column title="原因 / 说明" data-index="reason" :width="330" ellipsis />
            <a-table-column title="操作" :width="90" fixed="right"><template #default="{ record }"><a-button type="link" size="small" @click="handleCopyAuditInfo(record)">复制</a-button></template></a-table-column>
          </a-table>

          <div class="pagination-shell">
            <span class="footer-total">共 {{ totalAuditSize }} 条记录</span>
            <a-pagination
              v-model:current="auditCurrentPage"
              v-model:page-size="auditPageSize"
              :page-size-options="['20', '40', '60', '80']"
              :show-total="totalText"
              :total="totalAuditSize"
              show-quick-jumper
              show-size-changer
              @change="handleAuditPageChange"
            />
            <span class="footer-spacer" />
          </div>
        </a-tab-pane>
      </a-tabs>
    </a-card>
  </section>
</template>

<style scoped>
.security-page {
  display: flex;
  flex-direction: column;
  align-self: stretch;
  width: 100%;
  max-width: none;
  min-width: 0;
  padding: 16px 18px 24px;
  box-sizing: border-box;
  gap: 12px;
  color: #344054;
}
.security-page :deep(.ant-card),
.security-page :deep(.ant-tabs),
.security-page :deep(.ant-tabs-content),
.security-page :deep(.ant-tabs-tabpane),
.security-page :deep(.ant-table-wrapper) {
  width: 100%;
  min-width: 0;
}
.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  padding: 4px 2px;
}
.page-header h2 { margin: 0; color: #182230; font-size: 22px; line-height: 32px; }
.page-header p { margin: 4px 0 0; color: #667085; font-size: 13px; }
.filter-card,
.audit-card { width: 100%; box-shadow: 0 1px 3px rgba(16, 24, 40, .06); }
.filter-card :deep(.ant-card-body) { padding: 16px 18px; }
.filter-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(220px, 1fr)) auto;
  align-items: end;
  width: 100%;
  gap: 14px;
}
.filter-grid label { display: flex; flex-direction: column; min-width: 0; gap: 6px; color: #475467; font-size: 13px; }
.filter-actions,
.management-actions { display: flex; align-items: center; gap: 8px; }
.audit-card :deep(.ant-card-body) { width: 100%; min-width: 0; padding: 0; }
.audit-tabs :deep(.ant-tabs-nav) { margin: 0; padding: 0 18px; }
.audit-tabs :deep(.ant-tabs-content-holder) { width: 100%; min-width: 0; }
.tab-label { display: inline-flex; align-items: center; gap: 7px; }
.tab-heading {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 64px;
  padding: 12px 18px;
  border-bottom: 1px solid #f0f1f3;
  box-sizing: border-box;
}
.tab-heading > div { display: flex; align-items: baseline; min-width: 0; gap: 12px; }
.tab-heading strong { color: #182230; font-size: 16px; }
.tab-heading span { overflow: hidden; color: #667085; font-size: 12px; text-overflow: ellipsis; white-space: nowrap; }
.security-table :deep(.ant-table) { width: 100%; }
.security-table :deep(.ant-table-cell) { color: #344054; font-size: 13px; }
.security-table :deep(.ant-table-thead > tr > th) { color: #475467; font-weight: 600; background: #f8fafc; }
.security-table :deep(.ant-table-placeholder) { height: 220px; }
.device-id,
code { color: #344054; font: 12px/18px ui-monospace, SFMono-Regular, Consolas, monospace; }
.reason-cell { display: flex; align-items: center; min-width: 0; gap: 6px; }
.reason-cell > span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.reason-cell :deep(.ant-tag) { flex: none; margin: 0; }
.secondary { margin-top: 2px; color: #98a2b3; font-size: 12px; }
.management-heading { border-bottom: 0; }
.management-filter {
  display: grid;
  grid-template-columns: repeat(3, minmax(180px, 1fr)) auto;
  align-items: center;
  padding: 0 18px 16px;
  gap: 10px;
}
.pagination-shell {
  display: grid;
  grid-template-columns: minmax(120px, 1fr) auto minmax(120px, 1fr);
  align-items: center;
  width: 100%;
  min-height: 68px;
  padding: 14px 18px;
  border-top: 1px solid #f0f1f3;
  box-sizing: border-box;
}
.footer-total { justify-self: start; color: #667085; font-size: 12px; }
.pagination-shell :deep(.ant-pagination) { justify-self: center; margin: 0; }
.footer-spacer { justify-self: end; width: 100%; }
@media (max-width: 1100px) {
  .filter-grid { grid-template-columns: repeat(2, minmax(180px, 1fr)); }
  .filter-actions { grid-column: 1 / -1; }
  .management-filter { grid-template-columns: repeat(2, minmax(180px, 1fr)); }
  .management-actions { justify-content: flex-start; }
}
@media (max-width: 760px) {
  .security-page { padding: 12px; }
  .page-header,
  .tab-heading { align-items: flex-start; flex-direction: column; }
  .filter-grid,
  .management-filter { grid-template-columns: 1fr; }
  .filter-actions,
  .management-actions { grid-column: auto; flex-wrap: wrap; }
  .tab-heading > div { align-items: flex-start; flex-direction: column; gap: 3px; }
  .pagination-shell { display: flex; align-items: center; flex-direction: column; gap: 12px; }
  .footer-spacer { display: none; }
}
</style>
