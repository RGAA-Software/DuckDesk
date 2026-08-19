<script setup lang="ts">
import Hls from 'hls.js'
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { notification } from 'ant-design-vue'
import type { Device } from '@/entity/device.ts'
import { queryDevices } from '@/model/device_api.ts'
import { queryLiveStatus, type LiveStatus } from '@/model/live_api.ts'
import type { AppInstance, AppNode, AppRow, InstanceState } from '@/entity/app_schedule.ts'
import { listAppRows, listInstances } from '@/model/app_api.ts'

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
let hls: Hls | null = null
let refreshTimer: number | undefined

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
    .sort((left, right) => sequenceOf(right.instance_id) - sequenceOf(left.instance_id))[0]
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

function choosePreferredNode() {
  const current = nodeOptions.value.find((target) => target.node.node_id === selectedNodeId.value)
  const preferred =
    current ||
    nodeOptions.value.find((target) => target.instance?.state === 'running') ||
    nodeOptions.value[0]
  selectedNodeId.value = preferred?.node.node_id || ''
  applyTarget(preferred)
}

const streamLabel = computed(() => status.value?.stream_id || '')

function destroyPlayer() {
  hls?.destroy()
  hls = null
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

  if (element.canPlayType('application/vnd.apple.mpegurl')) {
    element.src = playUrl
    try {
      await element.play()
    } catch {
      // Autoplay is best-effort; native controls remain available.
    }
    return
  }
  if (!Hls.isSupported()) {
    playerError.value = '当前浏览器不支持 HLS 播放。'
    return
  }
  hls = new Hls({
    liveSyncDurationCount: 3,
    maxLiveSyncPlaybackRate: 1.5,
  })
  hls.on(Hls.Events.ERROR, (_event, data) => {
    if (data.fatal) {
      playerError.value = `播放失败：${data.type}`
    }
  })
  hls.loadSource(playUrl)
  hls.attachMedia(element)
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
      playerError.value = '查询直播状态失败，请检查 CMS 登录状态和媒体服务器配置。'
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
      selectedAppId.value =
        apps.value.find((app) =>
          app.nodes.some((node) => latestInstance(app.app_id, node)?.state === 'running'),
        )?.app_id ||
        apps.value[0]?.app_id ||
        ''
    }
    if (chooseDefault || !selectedTarget.value) choosePreferredNode()
  } finally {
    catalogLoading.value = false
  }
}

watch([selectedAppId, selectedNodeId], () => {
  applyTarget(selectedTarget.value)
  status.value = null
  playerError.value = ''
  destroyPlayer()
})

watch(selectedAppId, () => choosePreferredNode())

onMounted(async () => {
  await refreshCatalog(true)
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
          @click="refreshAndPlay"
          >刷新并观看</a-button
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
        选择节点后自动使用其设备 ID
        与应用标识。主流命名：&lt;device_id&gt;__app__&lt;app_id&gt;；观看端仅拉取 HLS 主流，不建立
        WebRTC 会话。
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
          message="H.265 推流在媒体服务器侧可以工作，但通用浏览器通常不能解码 HEVC。切换回 H.264 后可直接在本页观看。"
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
