<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { message, Modal } from 'ant-design-vue'
import type { AppInstance, AppNode, AppRow, InstanceState } from '@/entity/app_schedule.ts'
import {
  deleteApp,
  deleteNode,
  listAppRows,
  listInstances,
  nextPort,
  saveApp,
  saveNode,
  startInstance,
  startNode,
  stopInstance,
} from '@/model/app_api.ts'
import { queryAllServiceConn } from '@/model/conn_api.ts'
import { queryDevices } from '@/model/device_api.ts'
import type { ServiceConn } from '@/entity/service_conn.ts'
import type { Device } from '@/entity/device.ts'
import { buildGameHookClientUrl } from '@/util/web_client_url.ts'

interface ViewNode extends AppNode {
  instance?: AppInstance
  online: boolean
}

interface ViewRow extends Omit<AppRow, 'nodes'> {
  nodes: ViewNode[]
}

const POLL_MS = 4000
const rowsRaw = ref<AppRow[]>([])
const instances = ref<AppInstance[]>([])
const services = ref<ServiceConn[]>([])
const devices = ref<Device[]>([])
const loading = ref(false)
const saving = ref(false)
const dialogVisible = ref(false)
const editing = ref(false)

const form = ref({
  app_id: '',
  name: '',
  game_path: '',
  default_game_args: '',
  encoder_fps: 60,
  encoder_bitrate: 20,
  encoder_format: 'h264',
})

const nodeDialogVisible = ref(false)
const nodeEditing = ref(false)
const nodeSaving = ref(false)
const nodeForm = ref({
  node_id: '',
  app_id: '',
  name: '',
  device_id: '',
  listen_port: 32000,
})

// 节列表弹窗（分页）
const nodeListVisible = ref(false)
const nodeListAppId = ref('')
const nodePage = ref(1)
const nodePageSize = ref(8)

const nodeListApp = computed<ViewRow | undefined>(() =>
  rows.value.find((r) => r.app_id === nodeListAppId.value),
)
const pagedNodes = computed<ViewNode[]>(() => {
  const nodes = nodeListApp.value?.nodes || []
  const start = (nodePage.value - 1) * nodePageSize.value
  return nodes.slice(start, start + nodePageSize.value)
})

function openNodeList(row: ViewRow) {
  nodeListAppId.value = row.app_id
  nodePage.value = 1
  nodeListVisible.value = true
}

const onlineIds = computed(() => new Set(services.value.map((s) => s.device_id)))

interface DeviceOption {
  device_id: string
  label: string
  online: boolean
}

// The device inventory is persistent, while `services` only contains machines
// connected right now. Keep their union so an offline machine can still be
// configured as an application node.
const deviceOptions = computed<DeviceOption[]>(() => {
  const options = new Map<string, DeviceOption>()
  for (const device of devices.value) {
    if (!device.device_id) continue
    options.set(device.device_id, {
      device_id: device.device_id,
      label: device.device_name ? `${device.device_name} (${device.device_id})` : device.device_id,
      online: onlineIds.value.has(device.device_id),
    })
  }
  for (const service of services.value) {
    if (!options.has(service.device_id)) {
      options.set(service.device_id, {
        device_id: service.device_id,
        label: service.device_id,
        online: true,
      })
    }
  }
  return [...options.values()].sort((a, b) => Number(b.online) - Number(a.online) || a.label.localeCompare(b.label))
})

function seqOf(id: string): number {
  const m = id.match(/^(?:inst|req)-(\d+)-/)
  return m ? Number(m[1]) : 0
}

/** Bind the live instance of a node. Only active states; historical
 * failed/stopped must not sticky-display as the current row. Legacy instances
 * (empty node_id) fall back to device+port matching. */
function activeInstanceOf(appId: string, node: AppNode): AppInstance | undefined {
  return instances.value
    .filter(
      (i) =>
        i.app_id === appId &&
        (i.node_id
          ? i.node_id === node.node_id
          : i.device_id === node.device_id && i.listen_port === node.listen_port) &&
        (i.state === 'running' || i.state === 'starting' || i.state === 'stopping'),
    )
    .slice()
    .sort((a, b) => seqOf(b.instance_id) - seqOf(a.instance_id))[0]
}

const rows = computed<ViewRow[]>(() =>
  rowsRaw.value.map((row) => ({
    ...row,
    nodes: (row.nodes || []).map((n) => ({
      ...n,
      instance: activeInstanceOf(row.app_id, n),
      online: onlineIds.value.has(n.device_id),
    })),
  })),
)

function stateOf(node: ViewNode): InstanceState {
  return node.instance?.state || 'stopped'
}

function stateTag(state: InstanceState): 'success' | 'warning' | 'error' | 'default' {
  if (state === 'running') return 'success'
  if (state === 'starting' || state === 'stopping') return 'warning'
  if (state === 'failed') return 'error'
  return 'default'
}

function stateText(state: InstanceState): string {
  switch (state) {
    case 'running':
      return '运行中'
    case 'starting':
      return '启动中'
    case 'stopping':
      return '停止中'
    case 'failed':
      return '失败'
    default:
      return '已停止'
  }
}

function deviceIp(deviceId: string): string {
  return devices.value.find((d) => d.device_id === deviceId)?.device_ip_addr || '127.0.0.1'
}

/** 折叠过长的程序路径：保留首尾，中间用 ... 替代（hover 显示完整路径）。 */
function collapsePath(path: string): string {
  if (!path) return '—'
  if (path.length <= 48) return path
  return `${path.slice(0, 24)}...${path.slice(-18)}`
}

function runningCount(row: ViewRow): number {
  return row.nodes.filter((n) => n.instance?.state === 'running').length
}

function onlineNodeCount(row: ViewRow): number {
  return row.nodes.filter((n) => n.online).length
}

function resetFormForCreate() {
  editing.value = false
  form.value = {
    app_id: '',
    name: '',
    game_path: '',
    default_game_args: '',
    encoder_fps: 60,
    encoder_bitrate: 20,
    encoder_format: 'h264',
  }
}

function openCreate() {
  resetFormForCreate()
  dialogVisible.value = true
}

function openEdit(row: ViewRow) {
  editing.value = true
  form.value = {
    app_id: row.app_id,
    name: row.name,
    game_path: row.game_path,
    default_game_args: row.default_game_args || '',
    encoder_fps: row.encoder_fps || 60,
    encoder_bitrate: row.encoder_bitrate || 20,
    encoder_format: row.encoder_format || 'h264',
  }
  dialogVisible.value = true
}

async function openNodeCreate(row: ViewRow) {
  nodeEditing.value = false
  const deviceId = services.value[0]?.device_id || deviceOptions.value[0]?.device_id || ''
  const port = deviceId ? ((await nextPort(deviceId)) ?? 32000) : 32000
  nodeForm.value = {
    node_id: '',
    app_id: row.app_id,
    name: '',
    device_id: deviceId,
    listen_port: port,
  }
  nodeDialogVisible.value = true
}

function openNodeEdit(node: ViewNode) {
  nodeEditing.value = true
  nodeForm.value = {
    node_id: node.node_id,
    app_id: node.app_id,
    name: node.name,
    device_id: node.device_id,
    listen_port: node.listen_port || 32000,
  }
  nodeDialogVisible.value = true
}

async function onNodeDeviceChange(deviceId: string) {
  if (nodeEditing.value) return
  nodeForm.value.listen_port = (await nextPort(deviceId)) ?? 32000
}

async function refresh() {
  loading.value = true
  try {
    const [rowsResult, instancesResult, servicesResult, devicesResult] = await Promise.allSettled([
      listAppRows(),
      listInstances(),
      queryAllServiceConn(),
      queryDevices('', '', '', '', 1, 200),
    ])
    if (rowsResult.status === 'fulfilled' && rowsResult.value) rowsRaw.value = rowsResult.value
    if (instancesResult.status === 'fulfilled' && instancesResult.value) instances.value = instancesResult.value
    if (servicesResult.status === 'fulfilled' && servicesResult.value) services.value = servicesResult.value
    if (devicesResult.status === 'fulfilled' && devicesResult.value) devices.value = devicesResult.value
  } finally {
    loading.value = false
  }
}

async function submitSave() {
  const f = form.value
  if (!f.name.trim()) {
    message.warning('请填写应用名称')
    return
  }
  if (!f.game_path.trim()) {
    message.warning('请填写程序路径')
    return
  }
  saving.value = true
  try {
    const result = await saveApp({
      app_id: editing.value ? f.app_id : undefined,
      name: f.name.trim(),
      game_path: f.game_path.trim(),
      default_game_args: f.default_game_args || undefined,
      encoder_fps: f.encoder_fps,
      encoder_bitrate: f.encoder_bitrate,
      encoder_format: f.encoder_format,
    })
    if (!result.ok) {
      message.error(result.message)
      return
    }
    message.success(editing.value ? '已更新' : '已保存')
    dialogVisible.value = false
    await refresh()
  } finally {
    saving.value = false
  }
}

async function submitNodeSave() {
  const f = nodeForm.value
  if (!f.device_id) {
    message.warning('请选择机器')
    return
  }
  if (!f.listen_port || f.listen_port < 32000) {
    message.warning('端口需 ≥ 32000')
    return
  }
  nodeSaving.value = true
  try {
    const result = await saveNode({
      node_id: nodeEditing.value ? f.node_id : undefined,
      app_id: f.app_id,
      name: f.name.trim() || undefined,
      device_id: f.device_id,
      listen_port: f.listen_port,
    })
    if (!result.ok) {
      message.error(result.message, 8)
      return
    }
    message.success(nodeEditing.value ? '节点已更新' : '节点已创建')
    nodeDialogVisible.value = false
    await refresh()
  } finally {
    nodeSaving.value = false
  }
}

/** antd v4 的 Modal.confirm 不返回 Promise（返回 { destroy, update }），
 * 这里包一层 Promise，保持原确认弹窗的“确定/取消”异步语义。 */
function confirmDialog(content: string): Promise<boolean> {
  return new Promise((resolve) => {
    Modal.confirm({
      title: '删除确认',
      content,
      okText: '确定',
      cancelText: '取消',
      onOk: () => resolve(true),
      onCancel: () => resolve(false),
    })
  })
}

async function handleDelete(row: ViewRow) {
  const busy = row.nodes.some((n) => {
    const s = stateOf(n)
    return s === 'running' || s === 'starting' || s === 'stopping'
  })
  if (busy) {
    message.warning('应用下有正在运行的节点，请先停止再删除')
    return
  }
  try {
    const ok = await confirmDialog(`确定删除应用「${row.name}」？其下所有节点会一并删除。`)
    if (!ok) return
  } catch {
    return
  }
  const result = await deleteApp(row.app_id)
  if (!result.ok) {
    message.error(result.message)
    return
  }
  message.success('已删除')
  await refresh()
}

async function handleDeleteNode(node: ViewNode) {
  try {
    const ok = await confirmDialog(`确定删除节点「${node.name}」？`)
    if (!ok) return
  } catch {
    return
  }
  const result = await deleteNode(node.node_id)
  if (!result.ok) {
    message.error(result.message, 8)
    return
  }
  message.success('已删除')
  await refresh()
  // 弹窗分页：当前页删空后回退一页
  if (pagedNodes.value.length === 0 && nodePage.value > 1) {
    nodePage.value -= 1
  }
}

/** 应用级启动:CMS 自动挑选一个空闲节点。 */
async function handleStart(row: ViewRow) {
  if (row.nodes.length === 0) {
    message.warning('应用还没有节点，请先「新建节点」')
    return
  }
  const result = await startInstance({ app_id: row.app_id })
  if (!result.ok) {
    message.error(result.message || '启动失败', 8)
    await refresh()
    return
  }
  if (result.data.state === 'failed') {
    message.error(result.data.error || '启动失败', 8)
    await refresh()
    return
  }
  const nodeName = row.nodes.find((n) => n.node_id === result.data.node_id)?.name
  message.success(`启动成功（${nodeName || '节点'}，端口 ${result.data.listen_port}）`)
  await refresh()
}

async function handleStartNode(node: ViewNode) {
  if (!node.online) {
    message.error('机器不在线')
    return
  }
  const st = stateOf(node)
  if (st === 'running' || st === 'starting') {
    message.warning('已在运行或启动中')
    return
  }
  const result = await startNode(node.node_id)
  if (!result.ok) {
    message.error(result.message || '启动失败', 8)
    await refresh()
    return
  }
  if (result.data.state === 'failed') {
    message.error(result.data.error || '启动失败', 8)
    await refresh()
    return
  }
  message.success(`启动成功（端口 ${result.data.listen_port || node.listen_port}）`)
  await refresh()
}

async function handleStop(node: ViewNode) {
  const id = node.instance?.instance_id
  if (!id) {
    message.warning('没有可停止的实例')
    return
  }
  const result = await stopInstance(id)
  if (!result.ok) {
    message.error(result.message)
    return
  }
  message.success('已下发停止')
  await refresh()
}

function handleOpenClient(node: ViewNode) {
  const inst = node.instance
  if (!inst || inst.state !== 'running' || !inst.listen_port) {
    message.warning('请先启动并等到「运行中」')
    return
  }
  const url = buildGameHookClientUrl(deviceIp(node.device_id), inst.listen_port, {
    deviceId: node.device_id,
    instanceId: inst.instance_id,
  })
  window.open(url, '_blank', 'noopener,noreferrer')
}

/** CMS 启动链接:浏览器打开 = 自动选节点/指定节点 → 启动 → 302 进 web client。 */
function launchUrl(path: string): string {
  return `${window.location.origin}/api/v1/app/control${path}?appkey=${localStorage.getItem('appkey')}`
}

async function copyLaunchUrl(path: string) {
  const url = launchUrl(path)
  try {
    await navigator.clipboard.writeText(url)
  } catch {
    // 非安全上下文等场景退化
    const ta = document.createElement('textarea')
    ta.value = url
    document.body.appendChild(ta)
    ta.select()
    document.execCommand('copy')
    document.body.removeChild(ta)
  }
  message.success(`启动链接已复制，发给任何人即可一键启动进流`, 4)
}

/** 分页总数文案（antd 的 show-total 需要函数）。 */
function nodeTotalText(total: number): string {
  return `共 ${total} 个`
}

let timer: number | undefined
onMounted(async () => {
  await refresh()
  timer = window.setInterval(refresh, POLL_MS)
})
onUnmounted(() => {
  if (timer !== undefined) window.clearInterval(timer)
})
</script>

<template>
  <div class="p-4">
    <div class="flex items-center justify-between mb-4">
      <div>
        <div class="text-lg font-semibold">应用</div>
        <div class="text-sm text-gray-500">
          应用是模板；节点 = 机器 + 端口。点「N 个节点」管理节点，应用启动会自动挑选一个空闲节点
        </div>
      </div>
      <div class="flex gap-2">
        <a-button @click="refresh">刷新</a-button>
        <a-button type="primary" @click="openCreate">新建应用</a-button>
      </div>
    </div>

    <a-alert
      v-if="services.length === 0"
      class="mb-3"
      type="warning"
      title="当前没有在线 Service；仍可查看和新增应用/节点。离线节点会标记为离线，待 px_service 连上 CMS 后即可启动。"
    />

    <a-table
      :data-source="rows"
      size="small"
      row-key="app_id"
      :loading="loading"
    >
      <template #emptyText>还没有应用，点右上角「新建应用」</template>
      <a-table-column data-index="name" title="名称" min-width="110" />
      <a-table-column title="程序路径" min-width="260">
        <template #default="{ record }">
          <a-popover placement="top" trigger="hover" :overlay-style="{ width: '520px' }">
            <template #content>
              <div class="path-pop">{{ record.game_path }}</div>
            </template>
            <span class="path-collapse">{{ collapsePath(record.game_path) }}</span>
          </a-popover>
        </template>
      </a-table-column>
      <a-table-column title="节点" width="150">
        <template #default="{ record }">
          <a-button type="link" @click="openNodeList(record)">
            {{ record.nodes.length }} 个节点
          </a-button>
          <a-tag v-if="runningCount(record) > 0" color="success" class="ml-1">
            {{ runningCount(record) }} 运行中
          </a-tag>
        </template>
      </a-table-column>
      <a-table-column title="机器状态" width="120">
        <template #default="{ record }">
          <a-tag v-if="record.nodes.length > 0" :color="onlineNodeCount(record) > 0 ? 'success' : 'default'">
            {{ onlineNodeCount(record) }}/{{ record.nodes.length }} 在线
          </a-tag>
          <span v-else class="text-gray-400">未配置节点</span>
        </template>
      </a-table-column>
      <a-table-column title="操作" width="320" fixed="right">
        <template #default="{ record }">
          <a-button type="link" @click="openEdit(record)">编辑</a-button>
          <a-button type="link" @click="openNodeCreate(record)">新建节点</a-button>
          <a-button
            type="link"
            :disabled="onlineNodeCount(record) === 0"
            @click="handleStart(record)"
          >
            启动
          </a-button>
          <a-button
            type="link"
            :disabled="record.nodes.length === 0"
            @click="copyLaunchUrl(`/app/launch/${record.app_id}`)"
          >
            链接
          </a-button>
          <a-button type="link" danger @click="handleDelete(record)">删除</a-button>
        </template>
      </a-table-column>
    </a-table>

    <a-modal
      v-model:open="nodeListVisible"
      :title="nodeListApp ? `「${nodeListApp.name}」的节点` : '节点列表'"
      width="960px"
      :mask-closable="false"
      :footer="null"
    >
      <div class="flex justify-between items-center mb-2">
        <span class="text-sm text-gray-500">
          共 {{ nodeListApp?.nodes.length || 0 }} 个节点；节点启动只影响本行，应用启动会自动挑选空闲节点
        </span>
        <a-button v-if="nodeListApp" type="primary" size="small" @click="openNodeCreate(nodeListApp)">
          新建节点
        </a-button>
      </div>
      <a-table :data-source="pagedNodes" size="small" row-key="node_id">
        <template #emptyText>还没有节点，点右上角「新建节点」添加</template>
        <a-table-column data-index="name" title="节点" min-width="90" />
        <a-table-column title="机器" min-width="140">
          <template #default="{ record: node }">
            <div>{{ node.device_id }}</div>
            <a-tag :color="node.online ? 'success' : 'default'" class="mt-1">
              {{ node.online ? '在线' : '离线' }}
            </a-tag>
          </template>
        </a-table-column>
        <a-table-column data-index="listen_port" title="端口" width="80" />
        <a-table-column title="状态" width="100">
          <template #default="{ record: node }">
            <a-tag :color="stateTag(stateOf(node))">
              {{ stateText(stateOf(node)) }}
            </a-tag>
          </template>
        </a-table-column>
        <a-table-column title="错误" min-width="200" ellipsis>
          <template #default="{ record: node }">
            <span v-if="node.instance?.error" class="err-text">{{ node.instance.error }}</span>
          </template>
        </a-table-column>
        <a-table-column title="操作" width="360" fixed="right">
          <template #default="{ record: node }">
            <a-button
              type="link"
              :disabled="!node.online || stateOf(node) === 'running' || stateOf(node) === 'starting'"
              @click="handleStartNode(node)"
            >
              启动
            </a-button>
            <a-button
              type="link"
              danger
              :disabled="!node.instance || stateOf(node) === 'stopped' || stateOf(node) === 'stopping'"
              @click="handleStop(node)"
            >
              停止
            </a-button>
            <a-button
              type="link"
              :disabled="stateOf(node) !== 'running'"
              @click="handleOpenClient(node)"
            >
              打开
            </a-button>
            <a-button
              type="link"
              :disabled="!node.online || stateOf(node) === 'running' || stateOf(node) === 'starting'"
              @click="copyLaunchUrl(`/app/node/launch/${node.node_id}`)"
            >
              链接
            </a-button>
            <a-button type="link" @click="openNodeEdit(node)">编辑</a-button>
            <a-button
              type="link"
              danger
              :disabled="stateOf(node) === 'running' || stateOf(node) === 'starting' || stateOf(node) === 'stopping'"
              @click="handleDeleteNode(node)"
            >
              删除
            </a-button>
          </template>
        </a-table-column>
      </a-table>
      <div class="flex justify-end mt-3">
        <a-pagination
          v-model:current="nodePage"
          v-model:page-size="nodePageSize"
          :total="nodeListApp?.nodes.length || 0"
          :page-size-options="[8, 16, 32]"
          :show-total="nodeTotalText"
          show-size-changer
          size="small"
        />
      </div>
    </a-modal>

    <a-modal
      v-model:open="dialogVisible"
      :title="editing ? '编辑应用' : '新建应用'"
      width="560px"
      :mask-closable="false"
      :footer="null"
    >
      <a-form :model="form" :label-col="{ style: { width: '100px' } }">
        <a-form-item label="应用名称" required>
          <a-input v-model:value="form.name" placeholder="例如 CarGame" />
        </a-form-item>
        <a-form-item label="程序路径" required>
          <a-input
            v-model:value="form.game_path"
            class="path-input"
            placeholder="完整绝对路径；目录名里的连续空格必须保留"
          />
          <div class="text-xs text-gray-400 mt-1">
            勿从网页表格复制路径（浏览器会把连续空格压成一个）。请从资源管理器地址栏粘贴。
          </div>
        </a-form-item>
        <a-form-item label="启动参数">
          <a-input v-model:value="form.default_game_args" placeholder="可选" />
        </a-form-item>
        <a-form-item label="编码">
          <div class="flex gap-2 items-center flex-wrap">
            <a-select v-model:value="form.encoder_format" style="width: 100px">
              <a-select-option value="h264">h264</a-select-option>
              <a-select-option value="h265">h265</a-select-option>
            </a-select>
            <a-input-number v-model:value="form.encoder_fps" :min="1" :max="120" />
            <span class="text-xs text-gray-400">fps</span>
            <a-input-number v-model:value="form.encoder_bitrate" :min="1" :max="200" />
            <span class="text-xs text-gray-400">Mbps</span>
          </div>
        </a-form-item>
      </a-form>
      <div class="mt-4 text-right">
        <a-button class="mr-2" @click="dialogVisible = false">取消</a-button>
        <a-button type="primary" :loading="saving" @click="submitSave">保存</a-button>
      </div>
    </a-modal>

    <a-modal
      v-model:open="nodeDialogVisible"
      :title="nodeEditing ? '编辑节点' : '新建节点'"
      width="520px"
      :mask-closable="false"
      :footer="null"
    >
      <a-form :model="nodeForm" :label-col="{ style: { width: '100px' } }">
        <a-form-item label="节点名称">
          <a-input v-model:value="nodeForm.name" placeholder="留空自动命名（节点1、节点2…）" />
        </a-form-item>
        <a-form-item label="机器" required>
          <a-select
            v-model:value="nodeForm.device_id"
            class="w-full"
            show-search
            placeholder="选择机器（离线也可配置）"
            @change="onNodeDeviceChange"
          >
            <a-select-option
              v-for="device in deviceOptions"
              :key="device.device_id"
              :value="device.device_id"
            >
              {{ device.label }}（{{ device.online ? '在线' : '离线' }}）
            </a-select-option>
          </a-select>
        </a-form-item>
        <a-form-item label="端口" required>
          <a-input-number v-model:value="nodeForm.listen_port" :min="32000" :max="65535" />
          <span class="ml-2 text-xs text-gray-400">按机器分配，默认从 32000 递增；冲突会提示</span>
        </a-form-item>
      </a-form>
      <div class="mt-4 text-right">
        <a-button class="mr-2" @click="nodeDialogVisible = false">取消</a-button>
        <a-button type="primary" :loading="nodeSaving" @click="submitNodeSave">保存</a-button>
      </div>
    </a-modal>
  </div>
</template>

<style scoped>
.path-collapse {
  color: #409eff;
  cursor: pointer;
  font-family: ui-monospace, Consolas, monospace;
  font-size: 12px;
}
.path-collapse:hover {
  text-decoration: underline;
}
.path-pop {
  white-space: pre-wrap;
  word-break: break-all;
  font-family: ui-monospace, Consolas, monospace;
  font-size: 12px;
}
.path-input :deep(.ant-input) {
  white-space: pre;
  font-family: ui-monospace, Consolas, monospace;
}
.err-text {
  color: #dc2626;
  white-space: pre-wrap;
  word-break: break-all;
  font-size: 12px;
}
</style>
