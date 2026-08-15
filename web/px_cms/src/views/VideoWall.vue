<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { notification } from 'ant-design-vue'
import type { Device } from '@/entity/device.ts'
import { queryDevices } from '@/model/device_api.ts'
import { buildWebClientUrl } from '@/util/web_client_url.ts'

type CellStatus = 'connecting' | 'loaded' | 'unreachable'

interface WallCell {
  deviceId: string
  deviceName: string
  ip: string
  port: string
  password: string
  src: string
  status: CellStatus
  // 每次连接自增,作为 iframe 的 key 强制重建,保证重新加载
  gen: number
}

const MAX_CELLS = 9
const LOAD_TIMEOUT_MS = 10000

const devices = ref<Device[]>([])
const selectedIds = ref<string[]>([])
const cells = ref<WallCell[]>([])
const globalPassword = ref('')

const loadTimers = new Map<string, number>()

// desktop_link: link://base64(json{did, ips[], rdpt, ...}),同 DevicesList 的解析逻辑
function parseDesktopLink(device: Device): { did: string; ip: string; port: string } | null {
  try {
    const raw = device.desktop_link.startsWith('link://')
      ? device.desktop_link.substring(7)
      : device.desktop_link
    const info = JSON.parse(atob(raw))
    // ips 元素可能是字符串,也可能是 {ip: "..."} 结构
    const first = info.ips?.[0]
    const ip = typeof first === 'string' ? first : first?.ip
    const port = info.rdpt
    if (!ip || !port) {
      return null
    }
    return { did: info.did || device.device_id, ip, port: String(port) }
  } catch (e) {
    console.error('parse desktop_link failed', e)
    return null
  }
}

function buildSrc(cell: WallCell): string {
  // ?c= 编码 deviceId/password,与 panel / web_client 对齐
  return buildWebClientUrl(cell.ip, cell.port, {
    deviceId: cell.deviceId,
    password: cell.password || undefined,
  })
}

function clearLoadTimer(deviceId: string) {
  const t = loadTimers.get(deviceId)
  if (t !== undefined) {
    window.clearTimeout(t)
    loadTimers.delete(deviceId)
  }
}

function connectCell(cell: WallCell) {
  clearLoadTimer(cell.deviceId)
  cell.status = 'connecting'
  cell.src = buildSrc(cell)
  cell.gen += 1
  loadTimers.set(
    cell.deviceId,
    window.setTimeout(() => {
      // 超时仍未触发 load,认为 render 不可达
      if (cell.status === 'connecting') {
        cell.status = 'unreachable'
      }
      loadTimers.delete(cell.deviceId)
    }, LOAD_TIMEOUT_MS),
  )
}

function onFrameLoad(cell: WallCell) {
  // 只证明页面本身加载出来了,WebRTC 是否出画面由 iframe 内的 web_client 自己展示
  clearLoadTimer(cell.deviceId)
  if (cell.status === 'connecting') {
    cell.status = 'loaded'
  }
}

const handleApplyGlobalPassword = () => {
  if (!globalPassword.value) {
    notification.warning({ message: '请先输入统一密码' })
    return
  }
  for (const cell of cells.value) {
    cell.password = globalPassword.value
    connectCell(cell)
  }
}

// antd select 无 multiple-limit 等价属性,在 change 里截断到上限,保持原多选上限行为
const handleSelectChange = (ids: string[]) => {
  if (ids.length > MAX_CELLS) {
    selectedIds.value = ids.slice(0, MAX_CELLS)
  }
}

// 勾选变化时重建格子,保留同一设备已输入的密码
watch(selectedIds, (ids) => {
  const oldById = new Map(cells.value.map((c) => [c.deviceId, c]))
  const next: WallCell[] = []
  for (const id of ids) {
    const device = devices.value.find((d) => d.device_id === id)
    if (!device) {
      continue
    }
    const old = oldById.get(id)
    if (old) {
      oldById.delete(id)
      next.push(old)
      continue
    }
    const info = parseDesktopLink(device)
    if (!info) {
      notification.error({ message: `设备「${device.device_name}」的链接缺少 IP 或端口信息` })
      continue
    }
    next.push({
      deviceId: info.did,
      deviceName: device.device_name,
      ip: info.ip,
      port: info.port,
      password: '',
      src: '',
      status: 'connecting',
      gen: 0,
    })
  }
  for (const removed of oldById.values()) {
    clearLoadTimer(removed.deviceId)
  }
  cells.value = next
  // 新格子先按无密码加载,等用户输入密码后再重连
  for (const cell of cells.value) {
    if (!cell.src) {
      connectCell(cell)
    }
  }
})

const gridStyle = computed(() => {
  const n = cells.value.length
  const cols = n <= 1 ? 1 : n <= 4 ? 2 : 3
  return {
    gridTemplateColumns: `repeat(${cols}, minmax(0, 1fr))`,
  }
})

const statusTagType = (status: CellStatus) => {
  if (status === 'loaded') return 'success'
  if (status === 'unreachable') return 'error'
  return 'warning'
}

const statusText = (status: CellStatus) => {
  if (status === 'loaded') return '页面已加载'
  if (status === 'unreachable') return '无法到达 render'
  return '连接中…'
}

onMounted(async () => {
  const result = await queryDevices('', '', '', '', 1, 100)
  devices.value = result ?? []
})

onUnmounted(() => {
  for (const t of loadTimers.values()) {
    window.clearTimeout(t)
  }
  loadTimers.clear()
})
</script>

<template>
  <div class="w-full">
    <a-card class="w-full">
      <div class="flex items-end flex-wrap">
        <div class="w-100 flex flex-col items-start">
          <span class="!text-sm">选择设备(最多 {{ MAX_CELLS }} 台)</span>
          <div class="h-2" />
          <a-select
            v-model:value="selectedIds"
            mode="multiple"
            :max-tag-count="MAX_CELLS"
            placeholder="勾选要上墙的设备"
            class="w-full"
            @change="handleSelectChange"
          >
            <a-select-option
              v-for="d in devices"
              :key="d.device_id"
              :label="`${d.device_name} (${d.device_id})`"
              :value="d.device_id"
              :disabled="!d.online"
            >
              <span>{{ d.device_name }} ({{ d.device_id }})</span>
              <a-tag class="!ml-2" size="small" :color="d.online ? 'success' : 'error'">
                {{ d.online ? '在线' : '离线' }}
              </a-tag>
            </a-select-option>
          </a-select>
        </div>

        <div class="w-5" />
        <div class="w-60 flex flex-col items-start">
          <span class="!text-sm">统一安全密码(同密码场景)</span>
          <div class="h-2" />
          <a-input-password
            v-model:value="globalPassword"
            placeholder="应用到所有格子"
            @pressEnter="handleApplyGlobalPassword"
          />
        </div>

        <div class="w-5" />
        <a-button type="primary" class="w-32" @click="handleApplyGlobalPassword">
          应用到全部
        </a-button>
      </div>
    </a-card>

    <div class="h-2" />

    <a-card class="w-full" :bordered="false">
      <template #title>
        <span class="text-lg font-bold text-slate-800">多画面墙</span>
      </template>

      <a-empty v-if="cells.length === 0" description="请先在上方勾选设备" />

      <div v-else class="wall-grid" :style="gridStyle">
        <div v-for="cell in cells" :key="cell.deviceId" class="wall-cell">
          <div class="flex items-center justify-between px-2 pt-2">
            <span class="font-bold text-sm truncate" :title="cell.deviceId">
              {{ cell.deviceName }} ({{ cell.deviceId }})
            </span>
            <a-tag size="small" :color="statusTagType(cell.status)">
              {{ statusText(cell.status) }}
            </a-tag>
          </div>

          <div class="flex items-center px-2 py-1">
            <a-input-password
              v-model:value="cell.password"
              size="small"
              placeholder="安全密码"
              @pressEnter="connectCell(cell)"
            />
            <div class="w-1" />
            <a-button size="small" type="primary" @click="connectCell(cell)">连接</a-button>
          </div>

          <div class="cell-body">
            <iframe
              v-if="cell.src"
              :key="cell.gen"
              :src="cell.src"
              class="cell-frame"
              @load="onFrameLoad(cell)"
            ></iframe>
          </div>
        </div>
      </div>
    </a-card>
  </div>
</template>

<style scoped>
.wall-grid {
  display: grid;
  gap: 8px;
}

.wall-cell {
  display: flex;
  flex-direction: column;
  border: 1px solid #d9d9d9;
  border-radius: 4px;
  overflow: hidden;
  height: 42vh;
  min-height: 280px;
}

.cell-body {
  flex: 1;
  min-height: 0;
  background: #000;
}

.cell-frame {
  width: 100%;
  height: 100%;
  border: none;
}
</style>
