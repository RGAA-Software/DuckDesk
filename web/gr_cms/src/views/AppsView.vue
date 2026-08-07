<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import type { AppInstance, AppRow, InstanceState } from '@/entity/app_schedule.ts'
import {
  deleteApp,
  listAppRows,
  listInstances,
  nextPort,
  saveApp,
  startInstance,
  stopInstance,
} from '@/model/app_api.ts'
import { queryAllServiceConn } from '@/model/conn_api.ts'
import { queryDevices } from '@/model/device_api.ts'
import type { ServiceConn } from '@/entity/service_conn.ts'
import type { Device } from '@/entity/device.ts'
import { buildGameHookClientUrl } from '@/util/web_client_url.ts'

interface ViewRow extends AppRow {
  instance?: AppInstance
  online: boolean
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
  device_id: '',
  game_path: '',
  default_game_args: '',
  encoder_fps: 60,
  encoder_bitrate: 20,
  encoder_format: 'h264',
  listen_port: 32000,
})

const onlineIds = computed(() => new Set(services.value.map((s) => s.device_id)))

const rows = computed<ViewRow[]>(() =>
  rowsRaw.value.map((row) => {
    const seqOf = (id: string) => {
      const m = id.match(/^(?:inst|req)-(\d+)-/)
      return m ? Number(m[1]) : 0
    }
    // Only bind live states. Historical failed/stopped must not sticky-display
    // as the current row (e.g. "失败 / game_exe_rel must be relative" after game is gone).
    const active = instances.value
      .filter(
        (i) =>
          i.app_id === row.app_id &&
          i.device_id === row.device_id &&
          (i.state === 'running' || i.state === 'starting' || i.state === 'stopping'),
      )
      .slice()
      .sort((a, b) => seqOf(b.instance_id) - seqOf(a.instance_id))[0]
    return {
      ...row,
      instance: active,
      online: onlineIds.value.has(row.device_id),
    }
  }),
)

function stateOf(row: ViewRow): InstanceState {
  return row.instance?.state || 'stopped'
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

async function resetFormForCreate() {
  editing.value = false
  const port = (await nextPort()) ?? 32000
  form.value = {
    app_id: '',
    name: '',
    device_id: '',
    game_path: '',
    default_game_args: '',
    encoder_fps: 60,
    encoder_bitrate: 20,
    encoder_format: 'h264',
    listen_port: port,
  }
}

async function openCreate() {
  await resetFormForCreate()
  dialogVisible.value = true
}

function openEdit(row: ViewRow) {
  editing.value = true
  form.value = {
    app_id: row.app_id,
    name: row.name,
    device_id: row.device_id,
    game_path: row.game_path,
    default_game_args: row.default_game_args || '',
    encoder_fps: row.encoder_fps || 60,
    encoder_bitrate: row.encoder_bitrate || 20,
    encoder_format: row.encoder_format || 'h264',
    listen_port: row.listen_port || 32000,
  }
  dialogVisible.value = true
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
  if (!f.device_id) {
    ElMessage.warning('请选择机器')
    return
  }
  if (!f.game_path.trim()) {
    ElMessage.warning('请填写程序路径')
    return
  }
  if (!f.listen_port || f.listen_port < 32000) {
    ElMessage.warning('端口需 ≥ 32000')
    return
  }
  saving.value = true
  try {
    const result = await saveApp({
      app_id: editing.value ? f.app_id : undefined,
      name: f.name.trim(),
      device_id: f.device_id,
      game_path: f.game_path.trim(),
      default_game_args: f.default_game_args || undefined,
      encoder_fps: f.encoder_fps,
      encoder_bitrate: f.encoder_bitrate,
      encoder_format: f.encoder_format,
      listen_port: f.listen_port,
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

async function handleDelete(row: ViewRow) {
  try {
    await ElMessageBox.confirm(`确定删除应用「${row.name}」？`, '删除确认', { type: 'warning' })
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

async function handleStart(row: ViewRow) {
  if (!row.online) {
    ElMessage.error('机器不在线')
    return
  }
  const st = stateOf(row)
  if (st === 'running' || st === 'starting') {
    ElMessage.warning('已在运行或启动中')
    return
  }
  const result = await startInstance({
    app_id: row.app_id,
    device_id: row.device_id,
    listen_port: row.listen_port || undefined,
  })
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
  ElMessage.success(`启动成功（端口 ${result.data.listen_port || row.listen_port}）`)
  await refresh()
}

async function handleStop(row: ViewRow) {
  const id = row.instance?.instance_id
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

function handleOpenClient(row: ViewRow) {
  const inst = row.instance
  if (!inst || inst.state !== 'running' || !inst.listen_port) {
    ElMessage.warning('请先启动并等到「运行中」')
    return
  }
  const url = buildGameHookClientUrl(deviceIp(row.device_id), inst.listen_port, {
    deviceId: row.device_id,
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
        <div class="text-sm text-gray-500">新建 → 选机器 → 填程序路径和端口 → 保存；列表里编辑/启停</div>
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
      <el-table-column label="机器" min-width="140">
        <template #default="{ row }">
          <div>{{ row.device_id }}</div>
          <el-tag :type="row.online ? 'success' : 'info'" size="small" class="mt-1">
            {{ row.online ? '在线' : '离线' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="程序路径" min-width="260" show-overflow-tooltip>
        <template #default="{ row }">
          <span class="path-pre">{{ row.game_path }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="listen_port" label="端口" width="80" />
      <el-table-column label="状态" width="100">
        <template #default="{ row }">
          <el-tag :type="stateTag(stateOf(row))" size="small">{{ stateText(stateOf(row)) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="错误" min-width="220" show-overflow-tooltip>
        <template #default="{ row }">
          <span v-if="row.instance?.error" class="err-text">{{ row.instance.error }}</span>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="280" fixed="right">
        <template #default="{ row }">
          <el-button link type="primary" @click="openEdit(row)">编辑</el-button>
          <el-button
            link
            type="success"
            :disabled="!row.online || stateOf(row) === 'running' || stateOf(row) === 'starting'"
            @click="handleStart(row)"
          >
            启动
          </el-button>
          <el-button
            link
            type="danger"
            :disabled="!row.instance || stateOf(row) === 'stopped' || stateOf(row) === 'stopping'"
            @click="handleStop(row)"
          >
            停止
          </el-button>
          <el-button link type="primary" :disabled="stateOf(row) !== 'running'" @click="handleOpenClient(row)">
            打开
          </el-button>
          <el-button
            link
            type="danger"
            :disabled="stateOf(row) === 'running' || stateOf(row) === 'starting' || stateOf(row) === 'stopping'"
            @click="handleDelete(row)"
          >
            删除
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog
      v-model="dialogVisible"
      :title="editing ? '编辑应用' : '新建应用'"
      width="560px"
    >
      <el-form label-width="100px">
        <el-form-item label="应用名称" required>
          <el-input v-model="form.name" placeholder="例如 CarGame" />
        </el-form-item>
        <el-form-item label="机器" required>
          <el-select v-model="form.device_id" class="w-full" filterable placeholder="选择在线机器">
            <el-option
              v-for="s in services"
              :key="s.device_id"
              :label="s.device_id"
              :value="s.device_id"
            />
          </el-select>
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
        <el-form-item label="端口" required>
          <el-input-number v-model="form.listen_port" :min="32000" :max="65535" />
          <span class="ml-2 text-xs text-gray-400">默认从 32000 递增；若已被占用会提示</span>
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
