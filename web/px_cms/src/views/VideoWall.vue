<script setup lang="ts">
import { computed, markRaw, nextTick, onMounted, onUnmounted, reactive, ref, watch } from 'vue'
import type { Device } from '@/entity/device.ts'
import { queryDevices } from '@/model/device_api.ts'
import { WallRtcSession, type WallRtcState, type WallRtcStats } from '@/model/wall_rtc.ts'

interface WallCell {
  device: Device
  state: WallRtcState | 'offline'
  message: string
  ip: string
  port: number
  stats: WallRtcStats
  stream: MediaStream | null
  // Structural public surface keeps Vue's reactive type unwrapping from
  // exposing the class' private RTCPeerConnection internals.
  session: Pick<WallRtcSession, 'start' | 'close'> | null
  retryTimer: number
  retryAttempt: number
}

const PAGE_SIZE = 9
const devices = ref<Device[]>([])
const loading = ref(false)
const currentPage = ref(1)
const videoElements = new Map<string, HTMLVideoElement>()
const cells = ref<WallCell[]>([])

const sortedDevices = computed(() =>
  [...devices.value].sort((a, b) => {
    if (a.online !== b.online) return a.online ? -1 : 1
    return `${a.device_name}\u0000${a.device_id}`.localeCompare(`${b.device_name}\u0000${b.device_id}`, 'zh-CN')
  }),
)
const total = computed(() => sortedDevices.value.length)
const pageDevices = computed(() => {
  const start = (currentPage.value - 1) * PAGE_SIZE
  return sortedDevices.value.slice(start, start + PAGE_SIZE)
})

const emptyStats = (): WallRtcStats => ({
  width: 0,
  height: 0,
  fps: 0,
  bitrateKbps: 0,
  rttMs: 0,
  codec: '',
})

function parseEndpoint(device: Device) {
  try {
    const raw = device.desktop_link_raw
      ? JSON.parse(device.desktop_link_raw)
      : JSON.parse(atob(device.desktop_link.replace(/^link:\/\//, '')))
    const first = raw.ips?.[0]
    return {
      ip: typeof first === 'string' ? first : first?.ip || '',
      port: Number(raw.rdpt || 0),
    }
  } catch {
    return { ip: '', port: 0 }
  }
}

function setVideoRef(deviceId: string, element: unknown) {
  if (!(element instanceof HTMLVideoElement)) {
    videoElements.delete(deviceId)
    return
  }
  videoElements.set(deviceId, element)
  const cell = cells.value.find((item) => item.device.device_id === deviceId)
  // 统计数据每秒更新会触发组件重渲染；不要反复给同一个 video 赋值
  // srcObject，否则 Chrome 会偶发把 readyState/currentTime 重置后再缓冲。
  if (cell?.stream && element.srcObject !== cell.stream) {
    element.srcObject = cell.stream
    void element.play().catch(() => undefined)
  }
}

function closeCells() {
  for (const cell of cells.value) {
    if (cell.retryTimer) window.clearTimeout(cell.retryTimer)
    cell.retryTimer = 0
    cell.session?.close(false)
    cell.session = null
    cell.stream = null
  }
  for (const video of videoElements.values()) video.srcObject = null
  videoElements.clear()
}

async function buildPage() {
  closeCells()
  cells.value = pageDevices.value.map((device) => {
    const endpoint = parseEndpoint(device)
    return reactive<WallCell>({
      device,
      state: device.online ? 'connecting' : 'offline',
      message: device.online ? '准备连接' : '设备离线',
      ip: endpoint.ip,
      port: endpoint.port,
      stats: emptyStats(),
      stream: null,
      session: null,
      retryTimer: 0,
      retryAttempt: 0,
    })
  })
  await nextTick()
  for (const cell of cells.value) {
    if (cell.device.online) connectCell(cell)
  }
}

function connectCell(cell: WallCell) {
  if (cell.retryTimer) window.clearTimeout(cell.retryTimer)
  cell.retryTimer = 0
  cell.session?.close(false)
  cell.stream = null
  cell.stats = emptyStats()
  const video = videoElements.get(cell.device.device_id)
  if (video) video.srcObject = null

  const session = markRaw(
    new WallRtcSession(cell.device.device_id, {
      onState: (state, message) => {
        cell.state = state
        cell.message = message ?? stateText(state)
        if (state === 'playing') {
          if (cell.retryTimer) window.clearTimeout(cell.retryTimer)
          cell.retryTimer = 0
          cell.retryAttempt = 0
        }
        if (state === 'error') scheduleReconnect(cell)
      },
      onStream: (stream) => {
        cell.stream = stream
        const target = videoElements.get(cell.device.device_id)
        if (target && target.srcObject !== stream) {
          target.srcObject = stream
          void target.play().catch(() => undefined)
        }
      },
      onStats: (stats) => (cell.stats = stats),
      onEndpoint: (ip, port) => {
        cell.ip = ip
        cell.port = port
      },
    }),
  )
  cell.session = session
  void session.start()
}

function scheduleReconnect(cell: WallCell) {
  if (cell.retryTimer || !cell.device.online) return
  const delays = [3000, 10000, 30000]
  const delay = delays[Math.min(cell.retryAttempt, delays.length - 1)] ?? 30000
  cell.retryAttempt += 1
  cell.message = `${cell.message}，${Math.round(delay / 1000)} 秒后重连`
  cell.retryTimer = window.setTimeout(() => {
    cell.retryTimer = 0
    if (cells.value.includes(cell) && cell.device.online) connectCell(cell)
  }, delay)
}

async function loadDevices() {
  loading.value = true
  try {
    const result = await queryDevices('', '', '', '', 1, 1000)
    devices.value = result ?? []
    const maxPage = Math.max(1, Math.ceil(devices.value.length / PAGE_SIZE))
    if (currentPage.value > maxPage) currentPage.value = maxPage
    await buildPage()
  } finally {
    loading.value = false
  }
}

function stateText(state: WallRtcState | 'offline') {
  if (state === 'playing') return '画面传输中'
  if (state === 'connected') return '媒体已连接'
  if (state === 'connecting') return '连接中…'
  if (state === 'error') return '连接失败'
  if (state === 'offline') return '设备离线'
  return '已断开'
}

function stateColor(state: WallRtcState | 'offline') {
  if (state === 'playing') return 'success'
  if (state === 'connecting' || state === 'connected') return 'processing'
  return 'error'
}

function videoInfo(cell: WallCell) {
  const s = cell.stats
  if (!s.width) return '等待视频信息'
  return `${s.codec || 'H264'} · ${s.width}×${s.height} · ${s.fps} fps · ${s.bitrateKbps} Kbps`
}

watch(currentPage, () => void buildPage())
onMounted(() => void loadDevices())
onUnmounted(closeCells)
</script>

<template>
  <div class="wall-page">
    <div class="wall-toolbar">
      <div>
        <h2>多画面墙</h2>
        <p>后台只读监看 · 无声音 · 不计入连接统计与审计</p>
      </div>
      <div class="toolbar-actions">
        <span>共 {{ total }} 台设备，在线优先</span>
        <a-button :loading="loading" @click="loadDevices">刷新设备</a-button>
      </div>
    </div>

    <a-empty v-if="!loading && cells.length === 0" description="暂无设备" />

    <div v-else class="wall-grid">
      <article v-for="cell in cells" :key="cell.device.device_id" class="wall-cell">
        <header class="cell-header">
          <div class="device-title">
            <strong :title="cell.device.device_name">{{ cell.device.device_name || '未命名设备' }}</strong>
            <span>{{ cell.device.device_id }}</span>
          </div>
          <a-tag :color="stateColor(cell.state)">{{ stateText(cell.state) }}</a-tag>
        </header>

        <div class="video-stage">
          <video
            :ref="(el) => setVideoRef(cell.device.device_id, el)"
            autoplay
            muted
            playsinline
          ></video>
          <div v-if="cell.state !== 'playing'" class="video-placeholder">
            <span>{{ cell.message }}</span>
            <a-button v-if="cell.device.online && cell.state === 'error'" size="small" @click="connectCell(cell)">
              重试
            </a-button>
          </div>
        </div>

        <footer class="cell-footer">
          <span>{{ cell.ip || '未上报 IP' }}{{ cell.port ? `:${cell.port}` : '' }}</span>
          <span>{{ videoInfo(cell) }}</span>
          <span v-if="cell.stats.rttMs">RTT {{ cell.stats.rttMs }} ms</span>
        </footer>
      </article>
    </div>

    <div v-if="total > PAGE_SIZE" class="wall-pagination">
      <a-pagination v-model:current="currentPage" :page-size="PAGE_SIZE" :total="total" :show-size-changer="false" />
    </div>
  </div>
</template>

<style scoped>
.wall-page { display: flex; flex-direction: column; gap: 12px; min-height: 100%; }
.wall-toolbar { display: flex; align-items: center; justify-content: space-between; padding: 14px 18px; background: #fff; border: 1px solid #e7eaf0; border-radius: 8px; }
.wall-toolbar h2 { margin: 0; color: #182235; font-size: 20px; font-weight: 700; }
.wall-toolbar p { margin: 4px 0 0; color: #7a8495; font-size: 13px; }
.toolbar-actions { display: flex; align-items: center; gap: 14px; color: #667085; font-size: 13px; }
.wall-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; }
.wall-cell { min-width: 0; overflow: hidden; background: #fff; border: 1px solid #dde2ea; border-radius: 7px; box-shadow: 0 2px 8px rgba(18, 32, 56, .05); }
.cell-header { display: flex; align-items: center; justify-content: space-between; gap: 8px; height: 48px; padding: 0 10px; }
.device-title { min-width: 0; display: flex; flex-direction: column; }
.device-title strong { overflow: hidden; color: #1d2939; font-size: 13px; line-height: 20px; text-overflow: ellipsis; white-space: nowrap; }
.device-title span { color: #8b95a5; font: 11px/16px ui-monospace, SFMono-Regular, Consolas, monospace; }
.video-stage { position: relative; aspect-ratio: 16 / 9; overflow: hidden; background: #0d1118; }
.video-stage video { display: block; width: 100%; height: 100%; object-fit: contain; background: #000; }
.video-placeholder { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 10px; color: #aeb7c5; font-size: 13px; background: radial-gradient(circle at center, #202938 0, #0d1118 72%); }
.cell-footer { display: flex; align-items: center; gap: 10px; min-height: 32px; padding: 5px 10px; overflow: hidden; color: #687386; font-size: 11px; white-space: nowrap; }
.cell-footer span:nth-child(2) { flex: 1; overflow: hidden; text-align: center; text-overflow: ellipsis; }
.wall-pagination { display: flex; justify-content: center; padding: 6px 0 12px; }
@media (max-width: 1200px) { .wall-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
@media (max-width: 760px) { .wall-grid { grid-template-columns: 1fr; } .wall-toolbar { align-items: flex-start; flex-direction: column; gap: 12px; } }
</style>
