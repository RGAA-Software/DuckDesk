<script setup lang="ts">
import Hls from 'hls.js'
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { notification } from 'ant-design-vue'
import type { Device } from '@/entity/device.ts'
import { queryDevices } from '@/model/device_api.ts'
import { queryLiveStatus, type LiveStatus } from '@/model/live_api.ts'

const devices = ref<Device[]>([])
const selectedDeviceId = ref('')
const appId = ref('cargame_debug')
const status = ref<LiveStatus | null>(null)
const loading = ref(false)
const video = ref<HTMLVideoElement | null>(null)
const playerError = ref('')
let hls: Hls | null = null
let refreshTimer: number | undefined

const selectedDevice = computed(() =>
  devices.value.find((device) => device.device_id === selectedDeviceId.value),
)

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
  if (!appId.value.trim()) {
    notification.warning({ message: '请输入应用标识' })
    return
  }
  loading.value = true
  try {
    const result = await queryLiveStatus(selectedDeviceId.value, appId.value)
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

watch([selectedDeviceId, appId], () => {
  status.value = null
  playerError.value = ''
  destroyPlayer()
})

onMounted(async () => {
  devices.value = (await queryDevices('', '', '', '', 1, 100)) ?? []
  const firstOnline = devices.value.find((device) => device.online)
  if (firstOnline) {
    selectedDeviceId.value = firstOnline.device_id
  }
  refreshTimer = window.setInterval(() => {
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
          <div class="mb-2 text-sm">设备</div>
          <a-select v-model:value="selectedDeviceId" class="w-full" placeholder="选择在线设备">
            <a-select-option v-for="device in devices" :key="device.device_id" :value="device.device_id" :disabled="!device.online">
              {{ device.device_name }} ({{ device.device_id }})
            </a-select-option>
          </a-select>
        </div>
        <div class="w-56">
          <div class="mb-2 text-sm">应用标识</div>
          <a-input v-model:value="appId" placeholder="例如 cargame_debug" @press-enter="refreshAndPlay" />
        </div>
        <a-button type="primary" :loading="loading" @click="refreshAndPlay">刷新并观看</a-button>
      </div>
      <div class="mt-3 text-xs text-slate-500">
        主流命名：&lt;device_id&gt;__app__&lt;app_id&gt;。观看端仅拉取 ZLMediaKit 的 HLS 主流，不建立 WebRTC 会话。
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
          <span v-if="status.width">{{ status.width }} × {{ status.height }} @ {{ status.fps }} fps</span>
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
