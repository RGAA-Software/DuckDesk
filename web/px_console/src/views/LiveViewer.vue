<script setup lang="ts">
import mpegts from 'mpegts.js'
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { notification } from 'ant-design-vue'
import type { Device } from '@/entity/device.ts'
import { queryDevices } from '@/model/device_api.ts'
import { queryLiveStatus, type LiveStatus } from '@/model/live_api.ts'
import type { AppInstance, AppNode, AppRow, InstanceState } from '@/entity/app_schedule.ts'
import { listAppRows, listInstances, startNode } from '@/model/app_api.ts'

const devices = ref<Device[]>([])
const apps = ref<AppRow[]>([])
const instances = ref<AppInstance[]>([])
const selectedAppId = ref('')
const selectedNodeId = ref('')
const selectedDeviceId = ref('')
const status = ref<LiveStatus | null>(null)
const loading = ref(false)
const catalogLoading = ref(false)
const video = ref<HTMLVideoElement | null>(null)
const playerError = ref('')
let flvPlayer: mpegts.Player | null = null
let refreshTimer: number | undefined
let selectionAutoPlayReady = false
let selectionRevision = 0

interface LiveNodeOption {
  app: AppRow
  node: AppNode
  instance?: AppInstance
}

const selectedApp = computed(() => apps.value.find((app) => app.app_id === selectedAppId.value))

function sequenceOf(instanceId: string): number {
  const match = instanceId.match(/^inst-(\d+)-/)
  return match ? Number(match[1]) : 0
}

function instanceStatePriority(state?: InstanceState): number {
  // Instance IDs are generated independently by each Console process and are not
  // a reliable chronology after a Console restart. Always prefer the active
  // instance for a node; use the ID only as a deterministic tie breaker.
  switch (state) {
    case 'running':
      return 5
    case 'starting':
      return 4
    case 'stopping':
      return 3
    case 'failed':
      return 2
    case 'stopped':
    default:
      return 1
  }
}

function latestInstance(appId: string, node: AppNode): AppInstance | undefined {
  return instances.value
    .filter(
      (instance) =>
        instance.app_id === appId &&
        (instance.node_id
          ? instance.node_id === node.node_id
          : instance.device_id === node.device_id && instance.listen_port === node.listen_port),
    )
    .slice()
    .sort(
      (left, right) =>
        instanceStatePriority(right.state) - instanceStatePriority(left.state) ||
        sequenceOf(right.instance_id) - sequenceOf(left.instance_id),
    )[0]
}

const nodeOptions = computed<LiveNodeOption[]>(() => {
  const app = selectedApp.value
  if (!app) return []
  return (app.nodes || []).map((node) => ({
    app,
    node,
    instance: latestInstance(app.app_id, node),
  }))
})

const selectedTarget = computed(() =>
  nodeOptions.value.find((target) => target.node.node_id === selectedNodeId.value),
)

function instanceStateText(state?: InstanceState): string {
  switch (state) {
    case 'running':
      return '运行中'
    case 'starting':
      return '启动中'
    case 'stopping':
      return '停止中'
    case 'failed':
      return '启动失败'
    case 'stopped':
      return '已停止'
    default:
      return '未启动'
  }
}

function deviceLabel(deviceId: string): string {
  const device = devices.value.find((item) => item.device_id === deviceId)
  return device?.device_name ? `${device.device_name} (${deviceId})` : deviceId
}

function applyTarget(target?: LiveNodeOption) {
  selectedDeviceId.value = target?.node.device_id || ''
}

function chooseDefaultNode() {
  const current = nodeOptions.value.find((target) => target.node.node_id === selectedNodeId.value)
  const target = current || nodeOptions.value[0]
  selectedNodeId.value = target?.node.node_id || ''
  applyTarget(target)
}

const streamLabel = computed(() => status.value?.stream_id || '')

function destroyPlayer() {
  flvPlayer?.destroy()
  flvPlayer = null
  if (video.value) {
    video.value.pause()
    video.value.removeAttribute('src')
    video.value.load()
  }
}

async function attachPlayer(playUrl: string) {
  destroyPlayer()
  playerError.value = ''
  await nextTick()
  const element = video.value
  if (!element) return

  if (!mpegts.isSupported()) {
    playerError.value = '当前浏览器不支持 MSE/HTTP-FLV 播放。'
    return
  }
  // mpegts.js creates its own XHR loader.  Give it an absolute same-origin
  // URL; unlike a native <video> element it does not consistently resolve
  // the Console relative path returned by the status API.
  const absolutePlayUrl = new URL(playUrl, window.location.origin).toString()
  flvPlayer = mpegts.createPlayer({
    type: 'flv',
    isLive: true,
    url: absolutePlayUrl,
    hasAudio: true,
    hasVideo: true,
  }, {
    enableWorker: false,
    enableStashBuffer: false,
    stashInitialSize: 64,
    lazyLoad: false,
    deferLoadAfterSourceOpen: false,
    // Two-stage live-edge control: small drift is recovered smoothly by
    // playback-rate adjustment; a backlog over two seconds is corrected by
    // seeking near the buffered live edge. This prevents latency accumulated
    // by a throttled/background tab from growing without bound.
    liveBufferLatencyChasing: true,
    liveBufferLatencyChasingOnPaused: false,
    liveBufferLatencyMaxLatency: 2.0,
    liveBufferLatencyMinRemain: 0.5,
    liveSync: true,
    liveSyncMaxLatency: 1.0,
    liveSyncTargetLatency: 0.45,
    liveSyncPlaybackRate: 1.1,
    autoCleanupSourceBuffer: true,
    // Both cleanup bounds must be configured together. mpegts.js defaults
    // autoCleanupMinBackwardDuration to 120 seconds; pairing that default
    // with the old 5-second maximum made it call SourceBuffer.remove() with
    // a negative end time as soon as playback reached roughly five seconds.
    autoCleanupMaxBackwardDuration: 30,
    autoCleanupMinBackwardDuration: 10,
  })
  flvPlayer.on(mpegts.Events.ERROR, (errorType, errorDetail) => {
    playerError.value = `播放失败：${errorType} (${errorDetail})`
  })
  flvPlayer.attachMediaElement(element)
  flvPlayer.load()
  try {
    await element.play()
  } catch {
    // Autoplay is best-effort; native controls remain available.
  }
}

async function refreshStatus(startPlayback = true) {
  if (!selectedDeviceId.value) {
    notification.warning({ message: '请先选择设备' })
    return
  }
  if (!selectedTarget.value) {
    notification.warning({ message: '请先选择应用节点' })
    return
  }
  loading.value = true
  try {
    const result = await queryLiveStatus(selectedDeviceId.value, selectedAppId.value)
    if (!result) {
      status.value = null
      playerError.value = '查询直播状态失败，请检查 Console 登录状态和媒体服务器配置。'
      return
    }
    status.value = result
    if (startPlayback && result.online && result.browser_playable && result.play_url) {
      await attachPlayer(result.play_url)
    } else if (!result.online || !result.browser_playable) {
      destroyPlayer()
    }
  } finally {
    loading.value = false
  }
}

function refreshAndPlay() {
  void refreshStatus(true)
}

const targetCanStart = computed(() => {
  const state = selectedTarget.value?.instance?.state
  return !state || state === 'stopped' || state === 'failed'
})

async function startAndWatch() {
  const target = selectedTarget.value
  if (!target) return
  if (!targetCanStart.value) {
    await refreshStatus(true)
    return
  }
  loading.value = true
  try {
    const result = await startNode(target.node.node_id)
    if (!result.ok) {
      notification.error({ message: '启动节点失败', description: result.message })
      return
    }
    notification.success({ message: '节点已启动，正在等待主流上线' })
    for (let attempt = 0; attempt < 20; attempt += 1) {
      await refreshCatalog(false)
      await refreshStatus(true)
      if (status.value?.online) return
      await new Promise((resolve) => window.setTimeout(resolve, 1000))
    }
    notification.warning({ message: '节点已启动，但主流尚未上线' })
  } finally {
    loading.value = false
  }
}

async function refreshCatalog(chooseDefault = false) {
  catalogLoading.value = true
  try {
    const [appsResult, instancesResult, devicesResult] = await Promise.allSettled([
      listAppRows(),
      listInstances(),
      queryDevices('', '', '', '', 1, 200),
    ])
    if (appsResult.status === 'fulfilled' && appsResult.value) apps.value = appsResult.value
    if (instancesResult.status === 'fulfilled' && instancesResult.value)
      instances.value = instancesResult.value
    if (devicesResult.status === 'fulfilled' && devicesResult.value)
      devices.value = devicesResult.value

    if (!apps.value.some((app) => app.app_id === selectedAppId.value)) {
      selectedAppId.value = apps.value[0]?.app_id || ''
    }
    if (chooseDefault || !selectedTarget.value) chooseDefaultNode()
  } finally {
    catalogLoading.value = false
  }
}

watch([selectedAppId, selectedNodeId], async () => {
  const revision = ++selectionRevision
  applyTarget(selectedTarget.value)
  status.value = null
  playerError.value = ''
  destroyPlayer()
  if (!selectionAutoPlayReady) return

  // Application changes can also replace the selected node in the same Vue
  // update. Wait for that selection to settle and only play the latest one.
  await nextTick()
  if (revision !== selectionRevision || !selectedTarget.value || !selectedDeviceId.value) return
  await refreshStatus(true)
})

watch(selectedAppId, () => chooseDefaultNode())

onMounted(async () => {
  await refreshCatalog(true)
  selectionAutoPlayReady = true
  if (selectedTarget.value && selectedDeviceId.value) await refreshStatus(true)
  refreshTimer = window.setInterval(() => {
    void refreshCatalog(false)
    // Keep the status fresh. Playback itself is not recreated while it is healthy.
    if (selectedDeviceId.value) void refreshStatus(false)
  }, 10000)
})

onUnmounted(() => {
  if (refreshTimer !== undefined) window.clearInterval(refreshTimer)
  destroyPlayer()
})
</script>

<template>
  <div class="w-full">
    <a-card>
      <div class="flex gap-3 flex-wrap items-end">
        <div class="w-72">
          <div class="mb-2 text-sm">应用</div>
          <a-select
            v-model:value="selectedAppId"
            class="w-full"
            placeholder="选择应用"
            :loading="catalogLoading"
          >
            <a-select-option v-for="app in apps" :key="app.app_id" :value="app.app_id">
              {{ app.name }} · {{ app.app_id }}
            </a-select-option>
          </a-select>
        </div>
        <div class="w-96">
          <div class="mb-2 text-sm">节点 / 实例</div>
          <a-select
            v-model:value="selectedNodeId"
            class="w-full"
            placeholder="选择节点"
            :loading="catalogLoading"
          >
            <a-select-option
              v-for="target in nodeOptions"
              :key="target.node.node_id"
              :value="target.node.node_id"
            >
              {{ target.node.name || target.node.node_id }} ·
              {{ deviceLabel(target.node.device_id) }} · 端口 {{ target.node.listen_port }} ·
              {{ instanceStateText(target.instance?.state) }}
            </a-select-option>
          </a-select>
        </div>
        <a-button :loading="catalogLoading" @click="refreshCatalog(true)">刷新列表</a-button>
        <a-button
          type="primary"
          :loading="loading"
          :disabled="!selectedTarget"
          @click="startAndWatch"
          >{{ targetCanStart ? '启动并观看' : '刷新并观看' }}</a-button
        >
      </div>
      <a-alert
        v-if="!selectedTarget && selectedApp"
        class="mt-3"
        type="warning"
        message="该应用尚未配置节点。请先到“应用调度”为它添加节点。"
        show-icon
      />
      <div
        v-else-if="selectedTarget"
        class="mt-3 rounded bg-slate-50 px-4 py-3 text-sm text-slate-600 flex gap-x-6 gap-y-2 flex-wrap"
      >
        <span><b>应用标识：</b>{{ selectedAppId }}</span>
        <span><b>节点：</b>{{ selectedTarget.node.name || selectedTarget.node.node_id }}</span>
        <span><b>设备：</b>{{ deviceLabel(selectedTarget.node.device_id) }}</span>
        <span><b>端口：</b>{{ selectedTarget.node.listen_port }}</span>
        <span><b>实例状态：</b>{{ instanceStateText(selectedTarget.instance?.state) }}</span>
      </div>
      <div class="mt-3 text-xs text-slate-500">
        进入页面或切换应用、节点后会自动拉取在线主流，并使用所选节点的设备 ID
        与应用标识。主流命名：&lt;device_id&gt;__app__&lt;app_id&gt;；观看端仅拉取 HTTP-FLV
        主流，不建立 WebRTC 会话。
      </div>
    </a-card>

    <div class="h-2" />

    <a-card>
      <template #title>直播观看</template>
      <template #extra>
        <a-tag v-if="status" :color="status.online ? 'success' : 'default'">
          {{ status.online ? '在线' : '未推流' }}
        </a-tag>
      </template>

      <a-empty v-if="!status" description="选择设备后点击“刷新并观看”" />
      <div v-else>
        <a-alert
          :type="status.online && status.browser_playable ? 'success' : 'warning'"
          :message="status.message"
          show-icon
        />
        <div class="mt-3 flex gap-4 text-sm text-slate-600 flex-wrap">
          <span>流：{{ streamLabel }}</span>
          <span>视频：{{ status.video_codec || '-' }}</span>
          <span>音频：{{ status.audio_codec || '-' }}</span>
          <span v-if="status.width"
            >{{ status.width }} × {{ status.height }} @ {{ status.fps }} fps</span
          >
          <span>观看者：{{ status.reader_count }}</span>
        </div>

        <div v-if="status.online && status.browser_playable" class="player-shell mt-3">
          <video ref="video" class="player" controls muted playsinline></video>
        </div>
        <a-alert v-if="playerError" class="mt-3" type="error" :message="playerError" show-icon />
        <a-alert
          v-if="status.online && !status.browser_playable"
          class="mt-3"
          type="info"
          message="H.265 推流在媒体服务器侧可以工作，但 Chrome 的通用 MSE 解码路径不保证 HEVC。切换回 H.264 后可直接在本页低延迟观看。"
          show-icon
        />
      </div>
    </a-card>
  </div>
</template>

<style scoped>
.player-shell {
  background: #000;
  border-radius: 6px;
  overflow: hidden;
  max-height: 72vh;
}

.player {
  display: block;
  width: 100%;
  max-height: 72vh;
  background: #000;
}
</style>
