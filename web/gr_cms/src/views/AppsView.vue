<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
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

function stateTag(state: InstanceState): 'success' | 'warning' | 'danger' | 'info' {
  if (state === 'running') return 'success'
  if (state === 'starting' || state === 'stopping') return 'warning'
  if (state === 'failed') return 'danger'
  return 'info'
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

function runningCount(row: ViewRow): number {
  return row.nodes.filter((n) => n.instance?.state === 'running').length
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
  const deviceId = services.value[0]?.device_id || ''
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
    const [r, i, s, d] = await Promise.all([
      listAppRows(),
      listInstances(),
      queryAllServiceConn(),
      queryDevices('', '', '', '', 1, 200),
    ])
    if (r) rowsRaw.value = r
    if (i) instances.value = i
    if (s) services.value = s
    if (d) devices.value = d
  } finally {
    loading.value = false
  }
}

async function submitSave() {
  const f = form.value
  if (!f.name.trim()) {
    ElMessage.warning('请填写应用名称')
    return
  }
  if (!f.game_path.trim()) {
    ElMessage.warning('请填写程序路径')
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
      ElMessage.error(result.message)
      return
    }
    ElMessage.success(editing.value ? '已更新' : '已保存')
    dialogVisible.value = false
    await refresh()
  } finally {
    saving.value = false
  }
}

async function submitNodeSave() {
  const f = nodeForm.value
  if (!f.device_id) {
    ElMessage.warning('请选择机器')
    return
  }
  if (!f.listen_port || f.listen_port < 32000) {
    ElMessage.warning('端口需 ≥ 32000')
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
      ElMessage.error({ message: result.message, duration: 8000, showClose: true })
      return
    }
    ElMessage.success(nodeEditing.value ? '节已更新' : '节已创建')
    nodeDialogVisible.value = false
    await refresh()
  } finally {
    nodeSaving.value = false
  }
}

async function handleDelete(row: ViewRow) {
  try {
    await ElMessageBox.confirm(`确定删除应用「${row.name}」？其下所有节会一并删除。`, '删除确认', {
      type: 'warning',
    })
  } catch {
    return
  }
  const result = await deleteApp(row.app_id)
  if (!result.ok) {
    ElMessage.error(result.message)
    return
  }
  ElMessage.success('已删除')
  await refresh()
}

async function handleDeleteNode(node: ViewNode) {
  try {
    await ElMessageBox.confirm(`确定删除节「${node.name}」？`, '删除确认', { type: 'warning' })
  } catch {
    return
  }
  const result = await deleteNode(node.node_id)
  if (!result.ok) {
    ElMessage.error({ message: result.message, duration: 8000, showClose: true })
    return
  }
  ElMessage.success('已删除')
  await refresh()
  // 弹窗分页：当前页删空后回退一页
  if (pagedNodes.value.length === 0 && nodePage.value > 1) {
    nodePage.value -= 1
  }
}

/** 应用级启动:CMS 自动挑选一个空闲节。 */
async function handleStart(row: ViewRow) {
  if (row.nodes.length === 0) {
    ElMessage.warning('应用还没有节，请先「新建节」')
    return
  }
  const result = await startInstance({ app_id: row.app_id })
  if (!result.ok) {
    ElMessage.error({ message: result.message || '启动失败', duration: 8000, showClose: true })
    await refresh()
    return
  }
  if (result.data.state === 'failed') {
    ElMessage.error({
      message: result.data.error || '启动失败',
      duration: 8000,
      showClose: true,
    })
    await refresh()
    return
  }
  const nodeName = row.nodes.find((n) => n.node_id === result.data.node_id)?.name
  ElMessage.success(
    `启动成功（${nodeName || '节'}，端口 ${result.data.listen_port}）`,
  )
  await refresh()
}

async function handleStartNode(node: ViewNode) {
  if (!node.online) {
    ElMessage.error('机器不在线')
    return
  }
  const st = stateOf(node)
  if (st === 'running' || st === 'starting') {
    ElMessage.warning('已在运行或启动中')
    return
  }
  const result = await startNode(node.node_id)
  if (!result.ok) {
    ElMessage.error({ message: result.message || '启动失败', duration: 8000, showClose: true })
    await refresh()
    return
  }
  if (result.data.state === 'failed') {
    ElMessage.error({
      message: result.data.error || '启动失败',
      duration: 8000,
      showClose: true,
    })
    await refresh()
    return
  }
  ElMessage.success(`启动成功（端口 ${result.data.listen_port || node.listen_port}）`)
  await refresh()
}

async function handleStop(node: ViewNode) {
  const id = node.instance?.instance_id
  if (!id) {
    ElMessage.warning('没有可停止的实例')
    return
  }
  const result = await stopInstance(id)
  if (!result.ok) {
    ElMessage.error(result.message)
    return
  }
  ElMessage.success('已下发停止')
  await refresh()
}

function handleOpenClient(node: ViewNode) {
  const inst = node.instance
  if (!inst || inst.state !== 'running' || !inst.listen_port) {
    ElMessage.warning('请先启动并等到「运行中」')
    return
  }
  const url = buildGameHookClientUrl(deviceIp(node.device_id), inst.listen_port, {
    deviceId: node.device_id,
    instanceId: inst.instance_id,
  })
  window.open(url, '_blank', 'noopener,noreferrer')
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
  <div class="p-4" v-loading="loading">
    <div class="flex items-center justify-between mb-4">
      <div>
        <div class="text-lg font-semibold">应用</div>
        <div class="text-sm text-gray-500">
          应用是模板；节 = 机器 + 端口。点「N 个节」管理节，应用启动会自动挑选一个空闲节
        </div>
      </div>
      <div class="flex gap-2">
        <el-button @click="refresh">刷新</el-button>
        <el-button type="primary" @click="openCreate">新建应用</el-button>
      </div>
    </div>

    <el-alert
      v-if="services.length === 0"
      class="mb-3"
      type="warning"
      :closable="false"
      title="当前没有在线 Service。请先让目标机器的 GammaRayService 连上 CMS。"
    />

    <el-table :data="rows" size="small" empty-text="还没有应用，点右上角「新建应用」">
      <el-table-column prop="name" label="名称" min-width="110" />
      <el-table-column label="程序路径" min-width="260" show-overflow-tooltip>
        <template #default="{ row }">
          <span class="path-pre">{{ row.game_path }}</span>
        </template>
      </el-table-column>
      <el-table-column label="节" width="150">
        <template #default="{ row }">
          <el-button link type="primary" @click="openNodeList(row)">
            {{ row.nodes.length }} 个节
          </el-button>
          <el-tag v-if="runningCount(row) > 0" size="small" type="success" class="ml-1">
            {{ runningCount(row) }} 运行中
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="260" fixed="right">
        <template #default="{ row }">
          <el-button link type="primary" @click="openEdit(row)">编辑</el-button>
          <el-button link type="primary" @click="openNodeCreate(row)">新建节</el-button>
          <el-button
            link
            type="success"
            :disabled="row.nodes.length === 0"
            @click="handleStart(row)"
          >
            启动
          </el-button>
          <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog
      v-model="nodeListVisible"
      :title="nodeListApp ? `「${nodeListApp.name}」的节` : '节列表'"
      width="960px"
    >
      <div class="flex justify-between items-center mb-2">
        <span class="text-sm text-gray-500">
          共 {{ nodeListApp?.nodes.length || 0 }} 个节；节启动只影响本行，应用启动会自动挑选空闲节
        </span>
        <el-button v-if="nodeListApp" type="primary" size="small" @click="openNodeCreate(nodeListApp)">
          新建节
        </el-button>
      </div>
      <el-table :data="pagedNodes" size="small" empty-text="还没有节，点右上角「新建节」添加">
        <el-table-column prop="name" label="节" min-width="90" />
        <el-table-column label="机器" min-width="140">
          <template #default="{ row: node }">
            <div>{{ node.device_id }}</div>
            <el-tag :type="node.online ? 'success' : 'info'" size="small" class="mt-1">
              {{ node.online ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="listen_port" label="端口" width="80" />
        <el-table-column label="状态" width="100">
          <template #default="{ row: node }">
            <el-tag :type="stateTag(stateOf(node))" size="small">
              {{ stateText(stateOf(node)) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="错误" min-width="200" show-overflow-tooltip>
          <template #default="{ row: node }">
            <span v-if="node.instance?.error" class="err-text">{{ node.instance.error }}</span>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="300" fixed="right">
          <template #default="{ row: node }">
            <el-button
              link
              type="success"
              :disabled="!node.online || stateOf(node) === 'running' || stateOf(node) === 'starting'"
              @click="handleStartNode(node)"
            >
              启动
            </el-button>
            <el-button
              link
              type="danger"
              :disabled="!node.instance || stateOf(node) === 'stopped' || stateOf(node) === 'stopping'"
              @click="handleStop(node)"
            >
              停止
            </el-button>
            <el-button
              link
              type="primary"
              :disabled="stateOf(node) !== 'running'"
              @click="handleOpenClient(node)"
            >
              打开
            </el-button>
            <el-button link type="primary" @click="openNodeEdit(node)">编辑</el-button>
            <el-button
              link
              type="danger"
              :disabled="stateOf(node) === 'running' || stateOf(node) === 'starting' || stateOf(node) === 'stopping'"
              @click="handleDeleteNode(node)"
            >
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
      <div class="flex justify-end mt-3">
        <el-pagination
          v-model:current-page="nodePage"
          v-model:page-size="nodePageSize"
          :total="nodeListApp?.nodes.length || 0"
          :page-sizes="[8, 16, 32]"
          layout="total, sizes, prev, pager, next"
          small
        />
      </div>
    </el-dialog>

    <el-dialog
      v-model="dialogVisible"
      :title="editing ? '编辑应用' : '新建应用'"
      width="560px"
    >
      <el-form label-width="100px">
        <el-form-item label="应用名称" required>
          <el-input v-model="form.name" placeholder="例如 CarGame" />
        </el-form-item>
        <el-form-item label="程序路径" required>
          <el-input
            v-model="form.game_path"
            class="path-input"
            placeholder="完整绝对路径；目录名里的连续空格必须保留"
          />
          <div class="text-xs text-gray-400 mt-1">
            勿从网页表格复制路径（浏览器会把连续空格压成一个）。请从资源管理器地址栏粘贴。
          </div>
        </el-form-item>
        <el-form-item label="启动参数">
          <el-input v-model="form.default_game_args" placeholder="可选" />
        </el-form-item>
        <el-form-item label="编码">
          <div class="flex gap-2 items-center flex-wrap">
            <el-select v-model="form.encoder_format" style="width: 100px">
              <el-option label="h264" value="h264" />
              <el-option label="h265" value="h265" />
            </el-select>
            <el-input-number v-model="form.encoder_fps" :min="1" :max="120" />
            <span class="text-xs text-gray-400">fps</span>
            <el-input-number v-model="form.encoder_bitrate" :min="1" :max="200" />
            <span class="text-xs text-gray-400">Mbps</span>
          </div>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submitSave">保存</el-button>
      </template>
    </el-dialog>

    <el-dialog
      v-model="nodeDialogVisible"
      :title="nodeEditing ? '编辑节' : '新建节'"
      width="520px"
      append-to-body
    >
      <el-form label-width="100px">
        <el-form-item label="节名称">
          <el-input v-model="nodeForm.name" placeholder="留空自动命名（节1、节2…）" />
        </el-form-item>
        <el-form-item label="机器" required>
          <el-select
            v-model="nodeForm.device_id"
            class="w-full"
            filterable
            placeholder="选择在线机器"
            @change="onNodeDeviceChange"
          >
            <el-option
              v-for="s in services"
              :key="s.device_id"
              :label="s.device_id"
              :value="s.device_id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="端口" required>
          <el-input-number v-model="nodeForm.listen_port" :min="32000" :max="65535" />
          <span class="ml-2 text-xs text-gray-400">按机器分配，默认从 32000 递增；冲突会提示</span>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="nodeDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="nodeSaving" @click="submitNodeSave">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<style scoped>
.path-pre {
  white-space: pre;
  font-family: ui-monospace, Consolas, monospace;
  font-size: 12px;
}
.path-input :deep(.el-input__inner) {
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
