<script setup lang="ts">
// 悬浮工具条:功能分组参考 C++ 端 src/gr_client/ui/float_controller_panel.cpp
// 第一行:本地功能(全屏/声音/仅观看);第二行:远程控制(走 media_data_channel)
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import type { PerfStats } from './rtc/stats'
import { SessionRecorder, recordFileName } from './rtc/recorder'
import {
  MSG_TYPE_HARD_UPDATE_DESKTOP,
  MSG_TYPE_LOCK_DEVICE,
  MSG_TYPE_MODIFY_FPS,
  MSG_TYPE_REQ_CTRL_ALT_DELETE,
  MSG_TYPE_STOP_RENDER,
  MSG_TYPE_SWITCH_MONITOR,
} from './rtc/control'

const props = defineProps<{
  connected: boolean
  // ft_data_channel 是否已就绪
  ftReady: boolean
  // 性能采样数据(App.vue 每 2s 更新)
  perf: PerfStats
  // 最近一次收到的远端剪贴板文本
  remoteClipboard: string
  // 发送控制消息(tc.Message 字段,camelCase),返回是否已发出
  send: (fields: Record<string, unknown>) => boolean
  // 「发送到远端」:读本地剪贴板并发往远端,返回是否成功
  sendClipboardToRemote: () => Promise<boolean>
  // 「复制到本地」:把 remoteClipboard 写入本地系统剪贴板
  copyRemoteToLocal: () => Promise<boolean>
  // 麦克风上行开关(getUserMedia + replaceTrack,App.vue 实现)
  toggleMic: () => Promise<void>
  // 取远端画面 video 元素(PiP/录制/指针锁定用);用 getter 避免 DOM 元素被响应式代理
  getVideo: () => HTMLVideoElement | null
  // 指针锁定状态(App.vue 监听 pointerlockchange 维护)
  pointerLocked: boolean
  // 手柄回传开关与状态(App.vue 的 GamepadController)
  gamepadOn: boolean
  gamepadStatus: string
  toggleGamepad: () => void
  log: (msg: string) => void
}>()

// 与 App 双向绑定的本地状态
const muted = defineModel<boolean>('muted', { required: true })
const micOn = defineModel<boolean>('micOn', { required: true })
const viewOnly = defineModel<boolean>('viewOnly', { required: true })
const ftVisible = defineModel<boolean>('ftVisible', { required: true })
const perfVisible = defineModel<boolean>('perfVisible', { required: true })

const expanded = ref(false)
const isFullscreen = ref(false)
const fps = ref(0) // 0 = 未知(尚未由本端设置)

const FPS_OPTIONS = [30, 60, 120]

// 性能面板显示值(码率单位为 kbps,>=1000 转 Mbps)
const perfBitrateText = computed(() => {
  const kbps = props.perf.videoBitrateKbps
  return kbps >= 1000 ? `${(kbps / 1000).toFixed(2)} Mbps` : `${kbps.toFixed(0)} kbps`
})
const perfResolutionText = computed(() =>
  props.perf.width > 0 ? `${props.perf.width}×${props.perf.height}` : '-',
)
const perfLossText = computed(() => `${(props.perf.lossRate * 100).toFixed(1)}%`)
const perfRttText = computed(() => (props.perf.rttMs > 0 ? `${props.perf.rttMs.toFixed(0)} ms` : '-'))
const perfJitterText = computed(() => `${props.perf.jitterMs.toFixed(1)} ms`)

async function onSendClipboard() {
  if (!(await props.sendClipboardToRemote())) {
    ElMessage.warning('读取本地剪贴板失败(需用户授权)或内容为空')
  }
}

async function onCopyRemote() {
  if (await props.copyRemoteToLocal()) {
    ElMessage.success('已复制到本地剪贴板')
  } else {
    ElMessage.warning('暂无远端剪贴板内容或写入失败')
  }
}

function toggleFullscreen() {
  if (document.fullscreenElement) {
    void document.exitFullscreen()
  } else {
    void document.documentElement.requestFullscreen()
  }
}

function onFullscreenChange() {
  isFullscreen.value = !!document.fullscreenElement
}

function toggleMute() {
  muted.value = !muted.value
}

// ---------- 画中画(PiP)----------
const pipActive = ref(false)

async function togglePip(): Promise<boolean> {
  const v = props.getVideo()
  if (!v) return false
  if (!('requestPictureInPicture' in v) || !document.pictureInPictureEnabled) {
    ElMessage.warning('当前浏览器不支持画中画')
    return false
  }
  try {
    if (document.pictureInPictureElement) {
      await document.exitPictureInPicture()
    } else {
      // 全屏与 PiP 互斥:先退全屏再进 PiP
      if (document.fullscreenElement) {
        await document.exitFullscreen()
        props.log('已退出全屏以进入画中画')
      }
      await v.requestPictureInPicture()
    }
    pipActive.value = !!document.pictureInPictureElement
    return pipActive.value
  } catch (err) {
    ElMessage.warning(`画中画失败: ${err instanceof Error ? err.message : String(err)}`)
    return false
  }
}

function onPipChange() {
  pipActive.value = !!document.pictureInPictureElement
}

// ---------- 本地录制(MediaRecorder -> webm)----------
const recorder = new SessionRecorder()
const recording = ref(false)
const recordSeconds = ref(0)
let recordTimer: number | null = null

const recordTimeText = computed(() => {
  const m = Math.floor(recordSeconds.value / 60)
  const s = recordSeconds.value % 60
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
})

function startRecord(): boolean {
  if (recording.value) return true
  if (!SessionRecorder.supported()) {
    ElMessage.warning('当前浏览器不支持 MediaRecorder 录制')
    return false
  }
  const stream = props.getVideo()?.srcObject as MediaStream | null
  if (!stream) {
    ElMessage.warning('尚无远端视频流,无法录制')
    return false
  }
  try {
    recorder.start(stream)
  } catch (err) {
    ElMessage.warning(`启动录制失败: ${err instanceof Error ? err.message : String(err)}`)
    return false
  }
  recording.value = true
  recordSeconds.value = 0
  recordTimer = window.setInterval(() => {
    recordSeconds.value += 1
  }, 1000)
  props.log(`开始录制 (${recorder.mimeType || '浏览器默认 webm'})`)
  return true
}

// download=false 供 CDP 调试钩子使用(不触发浏览器下载)
async function stopRecord(download = true): Promise<{ size: number; mimeType: string } | null> {
  if (recordTimer !== null) {
    window.clearInterval(recordTimer)
    recordTimer = null
  }
  try {
    const r = await recorder.stop()
    recording.value = false
    if (download) {
      const url = URL.createObjectURL(r.blob)
      const a = document.createElement('a')
      a.href = url
      a.download = recordFileName()
      a.click()
      URL.revokeObjectURL(url)
    }
    props.log(`录制完成: ${(r.blob.size / 1024).toFixed(1)} KB, ${r.seconds.toFixed(1)}s, ${r.mimeType}`)
    return { size: r.blob.size, mimeType: r.mimeType }
  } catch (err) {
    recording.value = false
    ElMessage.warning(`停止录制失败: ${err instanceof Error ? err.message : String(err)}`)
    return null
  }
}

function toggleRecord() {
  if (recording.value) {
    void stopRecord(true)
  } else {
    startRecord()
  }
}

// ---------- 指针锁定(相对鼠标模式)----------
async function togglePointerLock() {
  const v = props.getVideo()
  if (!v) return
  if (document.pointerLockElement) {
    document.exitPointerLock()
    return
  }
  try {
    await v.requestPointerLock()
  } catch (err) {
    ElMessage.warning(`锁定鼠标失败: ${err instanceof Error ? err.message : String(err)}`)
  }
}

function emit(fields: Record<string, unknown>, desc: string) {
  if (props.send(fields)) {
    props.log(`已发送控制消息: ${desc}`)
  } else {
    ElMessage.warning('数据通道未连接,发送失败')
  }
}

function sendCtrlAltDelete() {
  // 对齐 ProtoMessageMaker::MakeCtrlAltDelete(type=330,空 ReqCtrlAltDelete 子消息)
  emit({ type: MSG_TYPE_REQ_CTRL_ALT_DELETE, reqCtrlAltDelete: {} }, 'Ctrl+Alt+Del')
}

function hardUpdate() {
  // 对齐 BaseWorkspace::SendHardUpdateDesktopMessage(type=341,无子消息)
  emit({ type: MSG_TYPE_HARD_UPDATE_DESKTOP }, '刷新画面(kHardUpdateDesktop)')
}

async function switchMonitor() {
  // /get/render/configuration 目前只上报当前采集显示器名,无多显示器列表;
  // 有列表时提供选择,否则按单显提示
  try {
    const resp = await fetch('/get/render/configuration')
    const result = (await resp.json()) as {
      code?: number
      data?: { monitor_name?: string; monitors?: string[] }
    }
    const monitors = result.data?.monitors ?? []
    if (monitors.length > 1) {
      const current = result.data?.monitor_name ?? ''
      const next = monitors.find((m) => m !== current) ?? monitors[0]
      // 对齐 BaseWorkspace::SendSwitchMonitorMessage(type=170,SwitchMonitor{name})
      emit({ type: MSG_TYPE_SWITCH_MONITOR, switchMonitor: { name: next } }, `切换显示器 -> ${next}`)
    } else {
      ElMessage.info('远端仅上报单个采集显示器,无需切换')
    }
  } catch {
    ElMessage.warning('获取显示器列表失败')
  }
}

function lockDevice() {
  // 对齐 ProtoMessageMaker::MakeLockDevice(type=328,空 LockDevice 子消息)
  emit({ type: MSG_TYPE_LOCK_DEVICE, lockDevice: {} }, '锁屏(kLockDevice)')
}

function modifyFps(value: number) {
  // 对齐 BaseWorkspace::SendModifyFpsMessage(type=480,ModifyFps{fps})
  emit({ type: MSG_TYPE_MODIFY_FPS, modifyFps: { fps: value } }, `改帧率 -> ${value}`)
  fps.value = value
}

async function stopRender() {
  try {
    await ElMessageBox.confirm('将结束远端 render 进程(服务会自动拉起),确认重启?', '重启 Render', {
      type: 'warning',
      confirmButtonText: '重启',
      cancelButtonText: '取消',
    })
  } catch {
    return
  }
  // 对齐 ProtoMessageMaker::MakeStopRender(type=329,空 StopRender 子消息)
  emit({ type: MSG_TYPE_STOP_RENDER, stopRender: {} }, '重启 render(kStopRender)')
}

// 无头/CDP 调试用:window.__pip / window.__rec
function exposePipRecDebug() {
  const w = window as unknown as { __pip?: unknown; __rec?: unknown }
  w.__pip = {
    toggle: () => togglePip(),
    active: () => !!document.pictureInPictureElement,
    supported: () => {
      const v = props.getVideo()
      return !!v && 'requestPictureInPicture' in v && document.pictureInPictureEnabled
    },
  }
  w.__rec = {
    recording: () => recording.value,
    seconds: () => recordSeconds.value,
    start: () => startRecord(),
    // 调试停止不触发浏览器下载,返回 { size, mimeType }
    stop: () => stopRecord(false),
  }
}

onMounted(() => {
  document.addEventListener('fullscreenchange', onFullscreenChange)
  // enter/leavepictureinpicture 在 video 上触发,捕获阶段挂 document 同步按钮状态
  document.addEventListener('enterpictureinpicture', onPipChange, true)
  document.addEventListener('leavepictureinpicture', onPipChange, true)
  exposePipRecDebug()
})
onBeforeUnmount(() => {
  document.removeEventListener('fullscreenchange', onFullscreenChange)
  document.removeEventListener('enterpictureinpicture', onPipChange, true)
  document.removeEventListener('leavepictureinpicture', onPipChange, true)
  if (recording.value) void stopRecord(false)
})
</script>

<template>
  <div class="float-toolbar">
    <transition name="el-zoom-in-bottom">
      <div v-show="expanded" class="panel">
        <div class="group">
          <span class="group-title">本地</span>
          <el-button size="small" @click="toggleFullscreen">
            {{ isFullscreen ? '退出全屏' : '全屏' }}
          </el-button>
          <el-button size="small" :type="muted ? 'default' : 'primary'" @click="toggleMute">
            {{ muted ? '取消静音' : '静音' }}
          </el-button>
          <!-- 麦克风上行:浏览器 mic -> render 扬声器,需已连接 -->
          <el-button
            size="small"
            :type="micOn ? 'primary' : 'default'"
            :disabled="!props.connected"
            @click="props.toggleMic"
          >
            {{ micOn ? '关闭麦克风' : '开启麦克风' }}
          </el-button>
          <el-button size="small" :disabled="!props.ftReady" @click="ftVisible = true">
            文件传输
          </el-button>
          <el-button
            size="small"
            class="perf-toggle"
            :type="perfVisible ? 'primary' : 'default'"
            :disabled="!connected"
            @click="perfVisible = !perfVisible"
          >
            性能
          </el-button>
          <el-button
            size="small"
            :type="pipActive ? 'primary' : 'default'"
            :disabled="!connected"
            @click="togglePip"
          >
            画中画
          </el-button>
          <el-button
            size="small"
            :type="recording ? 'danger' : 'default'"
            :disabled="!connected"
            @click="toggleRecord"
          >
            {{ recording ? `■ 录制中 ${recordTimeText}` : '录制' }}
          </el-button>
          <el-button
            size="small"
            :type="props.pointerLocked ? 'warning' : 'default'"
            :disabled="!connected"
            @click="togglePointerLock"
          >
            {{ props.pointerLocked ? '解除锁定(Esc)' : '锁定鼠标' }}
          </el-button>
          <el-button
            size="small"
            :type="props.gamepadOn ? 'primary' : 'default'"
            :disabled="!connected"
            @click="props.toggleGamepad"
          >
            {{ props.gamepadOn ? '关闭手柄' : '手柄' }}
          </el-button>
          <span v-if="props.gamepadOn" class="gamepad-status" :title="props.gamepadStatus">
            🎮 {{ props.gamepadStatus || '检测中...' }}
          </span>
          <el-checkbox v-model="viewOnly" class="view-only-checkbox">仅观看</el-checkbox>
        </div>
        <!-- 性能面板:每 2s 采样 pc.getStats();码率为 WebRTC 自适应值(协议无改码率消息) -->
        <div v-if="perfVisible" class="group perf-panel">
          <div class="perf-item">
            <span class="perf-label">码率</span>
            <span class="perf-value perf-bitrate">{{ perfBitrateText }}</span>
          </div>
          <div class="perf-item">
            <span class="perf-label">帧率</span>
            <span class="perf-value perf-fps">{{ props.perf.fps.toFixed(0) }} fps</span>
          </div>
          <div class="perf-item">
            <span class="perf-label">RTT</span>
            <span class="perf-value perf-rtt">{{ perfRttText }}</span>
          </div>
          <div class="perf-item">
            <span class="perf-label">丢包</span>
            <span class="perf-value perf-loss">{{ perfLossText }}</span>
          </div>
          <div class="perf-item">
            <span class="perf-label">抖动</span>
            <span class="perf-value perf-jitter">{{ perfJitterText }}</span>
          </div>
          <div class="perf-item">
            <span class="perf-label">分辨率</span>
            <span class="perf-value perf-resolution">{{ perfResolutionText }}</span>
          </div>
          <span class="perf-note">码率为 WebRTC 自适应,协议不支持手动指定</span>
        </div>
        <!-- 剪贴板文本同步:浏览器权限限制下为按钮触发式 -->
        <div class="group">
          <span class="group-title">剪贴板</span>
          <el-button size="small" :disabled="!connected" @click="onSendClipboard">
            发送到远端
          </el-button>
          <template v-if="props.remoteClipboard">
            <span class="clipboard-preview" :title="props.remoteClipboard">
              远端: {{ props.remoteClipboard.slice(0, 30) }}
            </span>
            <el-button size="small" class="clipboard-copy" @click="onCopyRemote">
              复制到本地
            </el-button>
          </template>
          <span v-else class="clipboard-empty">远端: (暂无)</span>
        </div>
        <div class="group">
          <span class="group-title">远程</span>
          <el-button size="small" :disabled="!connected" @click="sendCtrlAltDelete">
            Ctrl+Alt+Del
          </el-button>
          <el-button size="small" :disabled="!connected" @click="hardUpdate">刷新画面</el-button>
          <el-button size="small" :disabled="!connected" @click="switchMonitor">切换显示器</el-button>
          <el-button size="small" :disabled="!connected" @click="lockDevice">锁屏</el-button>
          <span class="fps-group">
            <span class="group-title">帧率</span>
            <el-button-group>
              <el-button
                v-for="f in FPS_OPTIONS"
                :key="f"
                size="small"
                :type="fps === f ? 'primary' : 'default'"
                :disabled="!connected"
                @click="modifyFps(f)"
              >
                {{ f }}
              </el-button>
            </el-button-group>
          </span>
          <el-button size="small" type="danger" plain :disabled="!connected" @click="stopRender">
            重启Render
          </el-button>
        </div>
      </div>
    </transition>
    <el-button class="fab" circle :type="expanded ? 'primary' : 'default'" @click="expanded = !expanded">
      {{ expanded ? '×' : '☰' }}
    </el-button>
  </div>
</template>

<style scoped>
.float-toolbar {
  position: absolute;
  right: 16px;
  bottom: 16px;
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 8px;
  z-index: 10;
}
.panel {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px 12px;
  background: rgba(30, 30, 30, 0.9);
  border-radius: 8px;
}
.group {
  display: flex;
  align-items: center;
  gap: 4px;
  flex-wrap: wrap;
}
.group-title {
  color: #aaa;
  font-size: 12px;
  margin-right: 4px;
}
.view-only-checkbox {
  --el-checkbox-text-color: #eee;
  margin-left: 4px;
  height: auto;
}
.gamepad-status {
  color: #6f6;
  font-size: 12px;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.fps-group {
  display: inline-flex;
  align-items: center;
}
.fab {
  width: 44px;
  height: 44px;
  font-size: 18px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.5);
}
.perf-panel {
  padding: 8px 10px;
  background: rgba(0, 0, 0, 0.35);
  border-radius: 6px;
  gap: 12px;
}
.perf-item {
  display: inline-flex;
  flex-direction: column;
  align-items: flex-start;
  min-width: 64px;
}
.perf-label {
  color: #888;
  font-size: 11px;
}
.perf-value {
  color: #6f6;
  font-family: monospace;
  font-size: 13px;
}
.perf-note {
  color: #666;
  font-size: 11px;
  align-self: center;
}
.clipboard-preview {
  color: #ddd;
  font-size: 12px;
  max-width: 220px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.clipboard-empty {
  color: #666;
  font-size: 12px;
}
</style>
