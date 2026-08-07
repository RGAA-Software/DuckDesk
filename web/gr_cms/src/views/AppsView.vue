<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { ElMessage, ElNotification } from 'element-plus'
import type {
  AppInstance,
  Application,
  AppPlacement,
  InstanceState,
} from '@/entity/app_schedule.ts'
import {
  createApplication,
  createPlacement,
  listApplications,
  listInstances,
  listPlacements,
  startInstance,
  stopInstance,
} from '@/model/app_api.ts'
import { queryAllServiceConn } from '@/model/conn_api.ts'
import { queryDevices } from '@/model/device_api.ts'
import type { ServiceConn } from '@/entity/service_conn.ts'
import type { Device } from '@/entity/device.ts'
import { buildGameHookClientUrl } from '@/util/web_client_url.ts'

const POLL_MS = 5000

const apps = ref<Application[]>([])
const placements = ref<AppPlacement[]>([])
const instances = ref<AppInstance[]>([])
const services = ref<ServiceConn[]>([])
const devices = ref<Device[]>([])
const loading = ref(false)

const createAppVisible = ref(false)
const createPlcVisible = ref(false)
const startVisible = ref(false)

const appForm = ref({
  name: '',
  game_exe_rel: '',
  default_game_args: '',
  encoder_fps: 60,
  encoder_bitrate: 20,
  encoder_format: 'h264',
})

const plcForm = ref({
  app_id: '',
  device_id: '',
  install_root: '',
})

const startForm = ref({
  app_id: '',
  device_id: '',
  listen_port: 0 as number,
})

const onlineDeviceIds = computed(() => new Set(services.value.map((s) => s.device_id)))

const placementsForStart = computed(() =>
  placements.value.filter((p) => p.app_id === startForm.value.app_id),
)

function appName(appId: string): string {
  return apps.value.find((a) => a.app_id === appId)?.name || appId
}

function stateTag(state: InstanceState): 'success' | 'warning' | 'danger' | 'info' {
  switch (state) {
    case 'running':
      return 'success'
    case 'starting':
    case 'stopping':
      return 'warning'
    case 'failed':
      return 'danger'
    default:
      return 'info'
  }
}

function deviceIp(deviceId: string): string {
  const d = devices.value.find((x) => x.device_id === deviceId)
  return d?.device_ip_addr || ''
}

async function refresh() {
  loading.value = true
  try {
    const [a, p, i, s, d] = await Promise.all([
      listApplications(),
      listPlacements(),
      listInstances(),
      queryAllServiceConn(),
      queryDevices('', '', '', '', 1, 200),
    ])
    if (a) apps.value = a
    if (p) placements.value = p
    if (i) instances.value = i
    if (s) services.value = s
    if (d) devices.value = d
  } finally {
    loading.value = false
  }
}

async function submitCreateApp() {
  if (!appForm.value.name.trim() || !appForm.value.game_exe_rel.trim()) {
    ElMessage.warning('请填写应用名与相对路径')
    return
  }
  const created = await createApplication({
    name: appForm.value.name.trim(),
    game_exe_rel: appForm.value.game_exe_rel.trim(),
    default_game_args: appForm.value.default_game_args || undefined,
    encoder_fps: appForm.value.encoder_fps,
    encoder_bitrate: appForm.value.encoder_bitrate,
    encoder_format: appForm.value.encoder_format,
  })
  if (!created) {
    ElMessage.error('创建应用失败')
    return
  }
  ElMessage.success('已创建应用')
  createAppVisible.value = false
  appForm.value = {
    name: '',
    game_exe_rel: '',
    default_game_args: '',
    encoder_fps: 60,
    encoder_bitrate: 20,
    encoder_format: 'h264',
  }
  await refresh()
}

async function submitCreatePlc() {
  if (!plcForm.value.app_id || !plcForm.value.device_id || !plcForm.value.install_root.trim()) {
    ElMessage.warning('请选择应用、机器并填写安装根目录')
    return
  }
  const created = await createPlacement({
    app_id: plcForm.value.app_id,
    device_id: plcForm.value.device_id,
    install_root: plcForm.value.install_root.trim(),
  })
  if (!created) {
    ElMessage.error('创建放置失败（可能已存在或参数无效）')
    return
  }
  ElMessage.success('已创建放置')
  createPlcVisible.value = false
  plcForm.value = { app_id: '', device_id: '', install_root: '' }
  await refresh()
}

async function submitStart() {
  if (!startForm.value.app_id || !startForm.value.device_id) {
    ElMessage.warning('请选择应用与目标机器')
    return
  }
  if (!onlineDeviceIds.value.has(startForm.value.device_id)) {
    ElMessage.error('目标 Service 不在线')
    return
  }
  const inst = await startInstance({
    app_id: startForm.value.app_id,
    device_id: startForm.value.device_id,
    listen_port: startForm.value.listen_port || undefined,
  })
  if (!inst) {
    ElMessage.error('启动失败（无 Placement / Service 离线）')
    return
  }
  ElMessage.success(`已下发启动：${inst.instance_id}`)
  startVisible.value = false
  await refresh()
}

async function handleStop(row: AppInstance) {
  const r = await stopInstance(row.instance_id)
  if (!r) {
    ElMessage.error('停止失败')
    return
  }
  ElMessage.success('已下发停止')
  await refresh()
}

function handleOpenClient(row: AppInstance) {
  if (row.state !== 'running' || !row.listen_port) {
    ElNotification({ message: '实例未就绪（需 running 且有 listen_port）', type: 'warning' })
    return
  }
  const ip = deviceIp(row.device_id)
  if (!ip) {
    ElNotification({ message: '找不到设备 IP，请确认设备已上报 desktop_link', type: 'error' })
    return
  }
  const url = buildGameHookClientUrl(ip, row.listen_port, {
    deviceId: row.device_id,
    instanceId: row.instance_id,
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
        <div class="text-lg font-semibold">应用调度</div>
        <div class="text-sm text-gray-500">
          Application → Placement（每机 install_root）→ 启停 game-hook 实例并打开 Client
        </div>
      </div>
      <div class="flex gap-2">
        <el-button @click="refresh">刷新</el-button>
        <el-button type="primary" @click="createAppVisible = true">新建应用</el-button>
        <el-button @click="createPlcVisible = true">新建放置</el-button>
        <el-button type="success" @click="startVisible = true">启动实例</el-button>
      </div>
    </div>

    <el-card class="mb-4" shadow="never">
      <template #header>应用目录</template>
      <el-table :data="apps" size="small">
        <el-table-column prop="name" label="名称" min-width="120" />
        <el-table-column prop="app_id" label="app_id" min-width="160" show-overflow-tooltip />
        <el-table-column prop="game_exe_rel" label="相对路径" min-width="180" show-overflow-tooltip />
        <el-table-column label="编码" min-width="120">
          <template #default="{ row }">
            {{ row.encoder_format }} / {{ row.encoder_fps }}fps / {{ row.encoder_bitrate }}Mbps
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-card class="mb-4" shadow="never">
      <template #header>
        <div class="flex justify-between">
          <span>机器放置</span>
          <span class="text-xs text-gray-400">在线 Service: {{ services.length }}</span>
        </div>
      </template>
      <el-table :data="placements" size="small">
        <el-table-column label="应用" min-width="120">
          <template #default="{ row }">{{ appName(row.app_id) }}</template>
        </el-table-column>
        <el-table-column prop="device_id" label="device_id" min-width="140" />
        <el-table-column label="在线" width="80">
          <template #default="{ row }">
            <el-tag :type="onlineDeviceIds.has(row.device_id) ? 'success' : 'info'" size="small">
              {{ onlineDeviceIds.has(row.device_id) ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="install_root" label="install_root" min-width="220" show-overflow-tooltip />
        <el-table-column prop="placement_id" label="placement_id" min-width="120" show-overflow-tooltip />
      </el-table>
    </el-card>

    <el-card shadow="never">
      <template #header>运行实例</template>
      <el-table :data="instances" size="small">
        <el-table-column label="应用" min-width="110">
          <template #default="{ row }">{{ appName(row.app_id) }}</template>
        </el-table-column>
        <el-table-column prop="device_id" label="机器" min-width="120" />
        <el-table-column prop="instance_id" label="instance_id" min-width="160" show-overflow-tooltip />
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="stateTag(row.state)" size="small">{{ row.state }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="listen_port" label="端口" width="80" />
        <el-table-column prop="pid" label="PID" width="80" />
        <el-table-column prop="error" label="错误" min-width="140" show-overflow-tooltip />
        <el-table-column label="操作" width="180" fixed="right">
          <template #default="{ row }">
            <el-button
              link
              type="primary"
              :disabled="row.state !== 'running'"
              @click="handleOpenClient(row)"
            >
              打开 Client
            </el-button>
            <el-button
              link
              type="danger"
              :disabled="row.state === 'stopped' || row.state === 'failed' || row.state === 'stopping'"
              @click="handleStop(row)"
            >
              停止
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog v-model="createAppVisible" title="新建应用" width="520px">
      <el-form label-width="110px">
        <el-form-item label="名称" required>
          <el-input v-model="appForm.name" placeholder="CarGame" />
        </el-form-item>
        <el-form-item label="相对路径" required>
          <el-input v-model="appForm.game_exe_rel" placeholder="CarGame/CarGame.exe" />
        </el-form-item>
        <el-form-item label="启动参数">
          <el-input v-model="appForm.default_game_args" placeholder="可选" />
        </el-form-item>
        <el-form-item label="编码">
          <div class="flex gap-2 w-full">
            <el-input v-model="appForm.encoder_format" style="width: 90px" />
            <el-input-number v-model="appForm.encoder_fps" :min="1" :max="120" />
            <el-input-number v-model="appForm.encoder_bitrate" :min="1" :max="200" />
          </div>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createAppVisible = false">取消</el-button>
        <el-button type="primary" @click="submitCreateApp">创建</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="createPlcVisible" title="新建放置" width="520px">
      <el-form label-width="110px">
        <el-form-item label="应用" required>
          <el-select v-model="plcForm.app_id" class="w-full" filterable placeholder="选择应用">
            <el-option
              v-for="a in apps"
              :key="a.app_id"
              :label="`${a.name} (${a.app_id})`"
              :value="a.app_id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="机器" required>
          <el-select v-model="plcForm.device_id" class="w-full" filterable placeholder="在线 Service">
            <el-option
              v-for="s in services"
              :key="s.device_id"
              :label="s.device_id"
              :value="s.device_id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="install_root" required>
          <el-input v-model="plcForm.install_root" placeholder="D:\Games" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createPlcVisible = false">取消</el-button>
        <el-button type="primary" @click="submitCreatePlc">创建</el-button>
      </template>
    </el-dialog>

    <el-dialog v-model="startVisible" title="启动实例" width="520px">
      <el-form label-width="110px">
        <el-form-item label="应用" required>
          <el-select
            v-model="startForm.app_id"
            class="w-full"
            filterable
            @change="startForm.device_id = ''"
          >
            <el-option
              v-for="a in apps"
              :key="a.app_id"
              :label="`${a.name} (${a.app_id})`"
              :value="a.app_id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="机器" required>
          <el-select v-model="startForm.device_id" class="w-full" filterable>
            <el-option
              v-for="p in placementsForStart"
              :key="p.placement_id"
              :label="`${p.device_id}${onlineDeviceIds.has(p.device_id) ? ' (在线)' : ' (离线)'} — ${p.install_root}`"
              :value="p.device_id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="端口">
          <el-input-number v-model="startForm.listen_port" :min="0" :max="65535" />
          <span class="ml-2 text-xs text-gray-400">0 = Service 自动分配</span>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="startVisible = false">取消</el-button>
        <el-button type="primary" @click="submitStart">启动</el-button>
      </template>
    </el-dialog>
  </div>
</template>
