// 悬浮球 + 弹出菜单面板:对齐 C++ 端 float_controller.cpp(40x40 白球可拖,位置按比例持久化)
// 与 float_controller_panel.cpp(白色圆角卡片:顶部快捷按钮行 + 菜单项 + 右侧二级子面板)
// API 与旧 FloatToolbar.vue 完全兼容(props/models 不变),App.vue 只需换组件名
<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  IconVolume,
  IconVolumeOff,
  IconMaximize,
  IconMinimize,
  IconPictureInPicture,
  IconSettings,
  IconDeviceDesktop,
  IconTransfer,
  IconPlayerRecord,
  IconChartBar,
  IconChevronRight,
  IconCheck,
  IconSend,
  IconClipboardCopy,
  IconKeyboard,
  IconRefresh,
  IconLock,
  IconRestore,
  IconGauge,
  IconAspectRatio,
  IconDevices,
  IconMouse,
  IconMicrophone,
  IconDeviceGamepad2,
} from '@tabler/icons-vue'
import type { PerfStats } from './rtc/stats'
import { SessionRecorder, recordFileName } from './rtc/recorder'
import { MSG_TYPE_CHANGE_MONITOR_RESOLUTION } from './rtc/proto'
import logoUrl from './assets/tc_icon.png'
import {
  MSG_TYPE_HARD_UPDATE_DESKTOP,
  MSG_TYPE_LOCK_DEVICE,
  MSG_TYPE_MODIFY_FPS,
  MSG_TYPE_REQ_CTRL_ALT_DELETE,
  MSG_TYPE_STOP_RENDER,
  MSG_TYPE_SWITCH_MONITOR,
} from './rtc/control'

interface MonitorSpec {
  name: string
  resolutions: Array<{ width: number; height: number }>
  currentWidth: number
  currentHeight: number
  primary: boolean
}

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
  // 远端显示器列表(kServerConfiguration,含可用分辨率)与当前采集显示器名
  monitors: MonitorSpec[]
  capturingMonitor: string
  log: (msg: string) => void
}>()

// 与 App 双向绑定的本地状态
const muted = defineModel<boolean>('muted', { required: true })
const micOn = defineModel<boolean>('micOn', { required: true })
const viewOnly = defineModel<boolean>('viewOnly', { required: true })
const ftVisible = defineModel<boolean>('ftVisible', { required: true })
const perfVisible = defineModel<boolean>('perfVisible', { required: true })

// ---------- 悬浮球位置(比例持久化)----------
const BALL_SIZE = 48
const LS_POS_KEY = 'gr_web_client.float_ball_pos'
const pos = reactive({ x: 0, y: 0 })
const panelOpen = ref(false)
// 二级子面板:null=收起,'control'=控制,'display'=显示
const subPanel = ref<'' | 'control' | 'display'>('')
// 三级子面板(挂在「显示」二级面板的菜单项上)
const subSubPanel = ref<'' | 'fps' | 'resolution' | 'monitor'>('')

function clampPos() {
  pos.x = Math.min(Math.max(0, pos.x), window.innerWidth - BALL_SIZE)
  pos.y = Math.min(Math.max(0, pos.y), window.innerHeight - BALL_SIZE)
}

function savePosRatio() {
  try {
    localStorage.setItem(
      LS_POS_KEY,
      JSON.stringify({ rx: pos.x / window.innerWidth, ry: pos.y / window.innerHeight }),
    )
  } catch {
    /* localStorage 不可用时忽略 */
  }
}

function restorePos() {
  let rx = -1
  let ry = -1
  try {
    const saved = JSON.parse(localStorage.getItem(LS_POS_KEY) ?? '{}') as { rx?: number; ry?: number }
    if (typeof saved.rx === 'number' && typeof saved.ry === 'number') {
      rx = saved.rx
      ry = saved.ry
    }
  } catch {
    /* 数据损坏时按默认位置 */
  }
  if (rx >= 0 && ry >= 0) {
    pos.x = rx * window.innerWidth
    pos.y = ry * window.innerHeight
  } else {
    // 默认:视频区右上角附近(避开顶部控制条)
    pos.x = window.innerWidth - BALL_SIZE - 24
    pos.y = 80
  }
  clampPos()
}

function onWindowResize() {
  restorePos()
}

// ---------- 拖动(与点击区分:移动 <5px 算点击)----------
let dragStart: { px: number; py: number; bx: number; by: number } | null = null
const dragging = ref(false)
let moved = false

function onBallPointerDown(ev: PointerEvent) {
  ;(ev.currentTarget as HTMLElement).setPointerCapture(ev.pointerId)
  dragStart = { px: ev.clientX, py: ev.clientY, bx: pos.x, by: pos.y }
  moved = false
}

function onBallPointerMove(ev: PointerEvent) {
  if (!dragStart) return
  const dx = ev.clientX - dragStart.px
  const dy = ev.clientY - dragStart.py
  if (!moved && Math.hypot(dx, dy) < 5) return
  if (!moved) {
    moved = true
    dragging.value = true
    closePanel() // 拖球收起面板
  }
  pos.x = dragStart.bx + dx
  pos.y = dragStart.by + dy
  clampPos()
}

function onBallPointerUp(ev: PointerEvent) {
  ;(ev.currentTarget as HTMLElement).releasePointerCapture(ev.pointerId)
  if (!dragStart) return
  if (moved) {
    savePosRatio()
  } else {
    togglePanel()
  }
  dragStart = null
  moved = false
  dragging.value = false
}

// ---------- 面板开关/定位 ----------
const ballRef = ref<HTMLElement | null>(null)
const panelRef = ref<HTMLElement | null>(null)
const subPanelRef = ref<HTMLElement | null>(null)

const PANEL_W = 250
const PANEL_GAP = 8
// 面板最大高度估计值,用于垂直方向防超出(实际 max-height 由 CSS 兜底)
const PANEL_MAX_H = 440

function togglePanel() {
  panelOpen.value = !panelOpen.value
  if (!panelOpen.value) {
    subPanel.value = ''
    subSubPanel.value = ''
  }
}

function closePanel() {
  panelOpen.value = false
  subPanel.value = ''
  subSubPanel.value = ''
}

// 球在右半屏时面板弹左侧,否则右侧
const panelOnLeft = computed(() => pos.x + BALL_SIZE / 2 > window.innerWidth / 2)

const panelStyle = computed(() => {
  const vw = window.innerWidth
  const vh = window.innerHeight
  let left = panelOnLeft.value ? pos.x - PANEL_GAP - PANEL_W : pos.x + BALL_SIZE + PANEL_GAP
  left = Math.min(Math.max(8, left), vw - PANEL_W - 8)
  let top = pos.y + BALL_SIZE / 2 - 60
  top = Math.min(Math.max(8, top), vh - PANEL_MAX_H - 8)
  return { left: `${left}px`, top: `${top}px`, width: `${PANEL_W}px` }
})

// 二级子面板:从主面板侧缘弹出(与主面板同侧)
const subPanelStyle = computed(() => {
  const vw = window.innerWidth
  const mainLeft = parseFloat(panelStyle.value.left)
  let left = panelOnLeft.value ? mainLeft - PANEL_GAP - PANEL_W : mainLeft + PANEL_W + PANEL_GAP
  left = Math.min(Math.max(8, left), vw - PANEL_W - 8)
  return { left: `${left}px`, top: panelStyle.value.top, width: `${PANEL_W}px` }
})

// 三级子面板:从二级面板侧缘继续弹出(同侧)
const subSubPanelStyle = computed(() => {
  const vw = window.innerWidth
  const subLeft = parseFloat(subPanelStyle.value.left)
  let left = panelOnLeft.value ? subLeft - PANEL_GAP - PANEL_W : subLeft + PANEL_W + PANEL_GAP
  left = Math.min(Math.max(8, left), vw - PANEL_W - 8)
  return { left: `${left}px`, top: subPanelStyle.value.top, width: `${PANEL_W}px` }
})

// 点面板外收起(全局 pointerdown)
function onGlobalPointerDown(ev: PointerEvent) {
  if (!panelOpen.value) return
  const t = ev.target as Node
  if (ballRef.value?.contains(t)) return
  if (panelRef.value?.contains(t)) return
  if (subPanelRef.value?.contains(t)) return
  closePanel()
}

function toggleSubPanel(name: 'control' | 'display') {
  subPanel.value = subPanel.value === name ? '' : name
  subSubPanel.value = ''
}

function toggleSubSubPanel(name: 'fps' | 'resolution' | 'monitor') {
  subSubPanel.value = subSubPanel.value === name ? '' : name
}

// ---------- 快捷按钮行:声音/全屏/画中画 ----------
const isFullscreen = ref(false)

function toggleMute() {
  muted.value = !muted.value
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

// ---------- 控制消息(走 media_data_channel)----------
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

function lockDevice() {
  // 对齐 ProtoMessageMaker::MakeLockDevice(type=328,空 LockDevice 子消息)
  emit({ type: MSG_TYPE_LOCK_DEVICE, lockDevice: {} }, '锁屏(kLockDevice)')
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

// ---------- 显示子面板:帧率/分辨率/切换显示器/锁定鼠标/麦克风/手柄 ----------
const fps = ref(0) // 0 = 未知(尚未由本端设置)
const FPS_OPTIONS = [30, 60, 120]

function modifyFps(value: number) {
  // 对齐 BaseWorkspace::SendModifyFpsMessage(type=480,ModifyFps{fps})
  emit({ type: MSG_TYPE_MODIFY_FPS, modifyFps: { fps: value } }, `改帧率 -> ${value}`)
  fps.value = value
  subSubPanel.value = ''
}

// render 未上报分辨率列表时的兜底常见档位
const FALLBACK_RESOLUTIONS = [
  { width: 1920, height: 1080 },
  { width: 1600, height: 900 },
  { width: 1366, height: 768 },
  { width: 1280, height: 720 },
]

// 当前采集显示器(取不到时退化为列表第一个)
const currentMonitor = computed(
  () => props.monitors.find((m) => m.name === props.capturingMonitor) ?? props.monitors[0] ?? null,
)
const resolutionOptions = computed(() =>
  currentMonitor.value?.resolutions.length ? currentMonitor.value.resolutions : FALLBACK_RESOLUTIONS,
)
// 当前分辨率:优先 config 里的 current_width/height,退化用性能面板的视频分辨率
const currentResolution = computed(() => {
  const m = currentMonitor.value
  if (m && m.currentWidth > 0) return { width: m.currentWidth, height: m.currentHeight }
  if (props.perf.width > 0) return { width: props.perf.width, height: props.perf.height }
  return null
})

function isCurrentResolution(w: number, h: number): boolean {
  return currentResolution.value?.width === w && currentResolution.value?.height === h
}

function changeResolution(w: number, h: number) {
  const name = props.capturingMonitor || currentMonitor.value?.name || ''
  // 对齐 ChangeMonitorResolution{type=200,monitor_name,target_width,target_height}
  emit(
    {
      type: MSG_TYPE_CHANGE_MONITOR_RESOLUTION,
      changeMonitorResolution: { monitorName: name, targetWidth: w, targetHeight: h },
    },
    `改分辨率 -> ${w}x${h} (${name || '默认显示器'})`,
  )
  subSubPanel.value = ''
}

// 切换显示器:列表来自 kServerConfiguration(monitors prop),不再走 /get/render/configuration
function sendSwitchMonitor(name: string) {
  // 对齐 BaseWorkspace::SendSwitchMonitorMessage(type=170,SwitchMonitor{name})
  emit({ type: MSG_TYPE_SWITCH_MONITOR, switchMonitor: { name } }, `切换显示器 -> ${name}`)
  subSubPanel.value = ''
}

// ---------- 剪贴板 ----------
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

// ---------- 菜单动作 ----------
function openFileTransfer() {
  if (!props.ftReady) return
  ftVisible.value = true
  closePanel()
}

function togglePerf() {
  perfVisible.value = !perfVisible.value
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
  restorePos()
  window.addEventListener('resize', onWindowResize)
  document.addEventListener('pointerdown', onGlobalPointerDown, true)
  document.addEventListener('fullscreenchange', onFullscreenChange)
  // enter/leavepictureinpicture 在 video 上触发,捕获阶段挂 document 同步按钮状态
  document.addEventListener('enterpictureinpicture', onPipChange, true)
  document.addEventListener('leavepictureinpicture', onPipChange, true)
  exposePipRecDebug()
})
onBeforeUnmount(() => {
  window.removeEventListener('resize', onWindowResize)
  document.removeEventListener('pointerdown', onGlobalPointerDown, true)
  document.removeEventListener('fullscreenchange', onFullscreenChange)
  document.removeEventListener('enterpictureinpicture', onPipChange, true)
  document.removeEventListener('leavepictureinpicture', onPipChange, true)
  if (recording.value) void stopRecord(false)
})
</script>

<template>
  <!-- 悬浮球 -->
  <div
    ref="ballRef"
    class="float-ball"
    :class="{ dragging }"
    :style="{ left: `${pos.x}px`, top: `${pos.y}px` }"
    title="控制面板"
    @pointerdown="onBallPointerDown"
    @pointermove="onBallPointerMove"
    @pointerup="onBallPointerUp"
  >
    <img class="ball-logo" :src="logoUrl" alt="GammaRay" draggable="false" />
  </div>

  <!-- 主面板 -->
  <div v-if="panelOpen" ref="panelRef" class="ball-panel" :style="panelStyle">
    <!-- 顶部快捷按钮行:声音/全屏/画中画 -->
    <div class="quick-row">
      <button class="quick-btn" :title="muted ? '取消静音' : '静音'" @click="toggleMute">
        <IconVolumeOff v-if="muted" :size="20" />
        <IconVolume v-else :size="20" />
      </button>
      <button class="quick-btn" :title="isFullscreen ? '退出全屏' : '全屏'" @click="toggleFullscreen">
        <IconMinimize v-if="isFullscreen" :size="20" />
        <IconMaximize v-else :size="20" />
      </button>
      <button
        class="quick-btn"
        :class="{ active: pipActive }"
        :disabled="!connected"
        title="画中画"
        @click="togglePip"
      >
        <IconPictureInPicture :size="20" />
      </button>
    </div>

    <!-- 菜单列表 -->
    <div class="menu">
      <button class="menu-item" :class="{ open: subPanel === 'control' }" @click="toggleSubPanel('control')">
        <span class="menu-icon"><IconSettings :size="17" /></span>
        <span class="menu-text">控制</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button class="menu-item" :class="{ open: subPanel === 'display' }" @click="toggleSubPanel('display')">
        <span class="menu-icon"><IconDeviceDesktop :size="17" /></span>
        <span class="menu-text">显示</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button class="menu-item" :disabled="!ftReady" @click="openFileTransfer">
        <span class="menu-icon"><IconTransfer :size="17" /></span>
        <span class="menu-text">文件传输</span>
        <span class="menu-state">{{ ftReady ? '' : '未就绪' }}</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="toggleRecord">
        <span class="menu-icon" :class="{ recording }"><IconPlayerRecord :size="17" /></span>
        <span class="menu-text" :class="{ recording }">
          {{ recording ? `停止录制 ${recordTimeText}` : '屏幕录制' }}
        </span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="togglePerf">
        <span class="menu-icon"><IconChartBar :size="17" /></span>
        <span class="menu-text">统计</span>
        <span class="menu-state">{{ perfVisible ? '开' : '' }}</span>
      </button>
    </div>
  </div>

  <!-- 二级子面板:控制 -->
  <div v-if="panelOpen && subPanel === 'control'" ref="subPanelRef" class="ball-panel sub" :style="subPanelStyle">
    <div class="menu">
      <button class="menu-item" :disabled="!connected" @click="onSendClipboard">
        <span class="menu-icon"><IconSend :size="16" /></span>
        <span class="menu-text">发送到远端剪贴板</span>
      </button>
      <button class="menu-item" :disabled="!remoteClipboard" @click="onCopyRemote">
        <span class="menu-icon"><IconClipboardCopy :size="16" /></span>
        <span class="menu-text">复制远端到本地</span>
      </button>
      <div class="menu-item static">
        <el-checkbox v-model="viewOnly" class="view-only-checkbox">仅观看</el-checkbox>
      </div>
      <button class="menu-item" :disabled="!connected" @click="sendCtrlAltDelete">
        <span class="menu-icon"><IconKeyboard :size="16" /></span>
        <span class="menu-text">Ctrl+Alt+Del</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="hardUpdate">
        <span class="menu-icon"><IconRefresh :size="16" /></span>
        <span class="menu-text">刷新画面</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="lockDevice">
        <span class="menu-icon"><IconLock :size="16" /></span>
        <span class="menu-text">锁定设备</span>
      </button>
      <button class="menu-item danger" :disabled="!connected" @click="stopRender">
        <span class="menu-icon"><IconRestore :size="16" /></span>
        <span class="menu-text">重启 Render</span>
      </button>
    </div>
  </div>

  <!-- 二级子面板:显示 -->
  <div v-if="panelOpen && subPanel === 'display'" ref="subPanelRef" class="ball-panel sub" :style="subPanelStyle">
    <div class="menu">
      <button
        class="menu-item"
        :class="{ open: subSubPanel === 'fps' }"
        :disabled="!connected"
        @click="toggleSubSubPanel('fps')"
      >
        <span class="menu-icon"><IconGauge :size="16" /></span>
        <span class="menu-text">帧率</span>
        <span class="menu-state">{{ fps > 0 ? fps : '' }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button
        class="menu-item"
        :class="{ open: subSubPanel === 'resolution' }"
        :disabled="!connected"
        @click="toggleSubSubPanel('resolution')"
      >
        <span class="menu-icon"><IconAspectRatio :size="16" /></span>
        <span class="menu-text">分辨率</span>
        <span class="menu-state">
          {{ currentResolution ? `${currentResolution.width}×${currentResolution.height}` : '' }}
        </span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button
        class="menu-item"
        :class="{ open: subSubPanel === 'monitor' }"
        :disabled="!connected"
        @click="toggleSubSubPanel('monitor')"
      >
        <span class="menu-icon"><IconDevices :size="16" /></span>
        <span class="menu-text">切换显示器</span>
        <span class="menu-state">{{ capturingMonitor }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="togglePointerLock">
        <span class="menu-icon"><IconMouse :size="16" /></span>
        <span class="menu-text">锁定鼠标</span>
        <span class="menu-state">{{ pointerLocked ? '已锁定' : '' }}</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="props.toggleMic">
        <span class="menu-icon"><IconMicrophone :size="16" /></span>
        <span class="menu-text">麦克风</span>
        <span class="menu-state">{{ micOn ? '开' : '关' }}</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="props.toggleGamepad">
        <span class="menu-icon"><IconDeviceGamepad2 :size="16" /></span>
        <span class="menu-text">手柄</span>
        <span class="menu-state">{{ gamepadOn ? '开' : '关' }}</span>
      </button>
      <div v-if="gamepadOn" class="gamepad-status" :title="props.gamepadStatus">
        {{ props.gamepadStatus || '检测中...' }}
      </div>
    </div>
  </div>

  <!-- 三级子面板:帧率 -->
  <div
    v-if="panelOpen && subPanel === 'display' && subSubPanel === 'fps'"
    class="ball-panel sub"
    :style="subSubPanelStyle"
  >
    <div class="menu">
      <button
        v-for="f in FPS_OPTIONS"
        :key="f"
        class="menu-item"
        @click="modifyFps(f)"
      >
        <span class="menu-icon check-icon"><IconCheck v-if="fps === f" :size="16" /></span>
        <span class="menu-text" :class="{ current: fps === f }">{{ f }} fps</span>
      </button>
    </div>
  </div>

  <!-- 三级子面板:分辨率 -->
  <div
    v-if="panelOpen && subPanel === 'display' && subSubPanel === 'resolution'"
    class="ball-panel sub"
    :style="subSubPanelStyle"
  >
    <div class="menu">
      <button
        v-for="r in resolutionOptions"
        :key="`${r.width}x${r.height}`"
        class="menu-item"
        @click="changeResolution(r.width, r.height)"
      >
        <span class="menu-icon check-icon">
          <IconCheck v-if="isCurrentResolution(r.width, r.height)" :size="16" />
        </span>
        <span class="menu-text" :class="{ current: isCurrentResolution(r.width, r.height) }">
          {{ r.width }} × {{ r.height }}
        </span>
      </button>
      <div v-if="!currentMonitor?.resolutions.length" class="panel-note">
        远端未上报分辨率列表,显示常见档位
      </div>
    </div>
  </div>

  <!-- 三级子面板:切换显示器(列表来自 kServerConfiguration) -->
  <div
    v-if="panelOpen && subPanel === 'display' && subSubPanel === 'monitor'"
    class="ball-panel sub"
    :style="subSubPanelStyle"
  >
    <div class="menu">
      <button
        v-for="m in monitors"
        :key="m.name"
        class="menu-item"
        :disabled="m.name === capturingMonitor"
        @click="sendSwitchMonitor(m.name)"
      >
        <span class="menu-icon check-icon">
          <IconCheck v-if="m.name === capturingMonitor" :size="16" />
        </span>
        <span class="menu-text" :class="{ current: m.name === capturingMonitor }" :title="m.name">
          {{ m.name }}
          <template v-if="m.currentWidth > 0"> ({{ m.currentWidth }}×{{ m.currentHeight }})</template>
          <template v-if="m.primary"> · 主</template>
        </span>
      </button>
      <div v-if="!monitors.length" class="panel-note">远端未上报显示器列表</div>
    </div>
  </div>
</template>

<style scoped>
.float-ball {
  position: fixed;
  width: 48px;
  height: 48px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: grab;
  user-select: none;
  touch-action: none;
  z-index: 60;
}
.ball-logo {
  width: 30px;
  height: 30px;
  object-fit: contain;
  pointer-events: none;
}
.float-ball:hover {
  background: #f5f5f5;
}
.float-ball:active,
.float-ball.dragging {
  background: #dfdfdf;
  cursor: grabbing;
}
.ball-panel {
  position: fixed;
  background: #fff;
  border-radius: 8px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.35);
  max-height: calc(100vh - 16px);
  overflow-y: auto;
  overflow-x: hidden;
  z-index: 59;
  padding: 6px;
}
.quick-row {
  display: flex;
  gap: 4px;
  padding: 2px 2px 6px;
  border-bottom: 1px solid #e4e7ed;
  margin-bottom: 4px;
}
.quick-btn {
  flex: 1;
  height: 34px;
  border: none;
  border-radius: 6px;
  background: transparent;
  cursor: pointer;
  line-height: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #303133;
}
.quick-btn:hover:not(:disabled) {
  background: #f5f5f5;
}
.quick-btn:active:not(:disabled) {
  background: #dfdfdf;
}
.quick-btn.active {
  background: #ecf5ff;
}
.quick-btn:disabled {
  opacity: 0.45;
  cursor: default;
}
.menu {
  display: flex;
  flex-direction: column;
}
.menu-item {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  box-sizing: border-box;
  padding: 9px 10px;
  border: none;
  border-radius: 6px;
  background: transparent;
  font-size: 13px;
  color: #303133;
  cursor: pointer;
  text-align: left;
}
.menu-item:hover:not(:disabled):not(.static) {
  background: #f5f5f5;
}
.menu-item:active:not(:disabled):not(.static) {
  background: #dfdfdf;
}
.menu-item.open {
  background: #ecf5ff;
}
.menu-item:disabled {
  opacity: 0.45;
  cursor: default;
}
.menu-item.static {
  cursor: default;
}
.menu-item.danger .menu-text {
  color: #f56c6c;
}
.menu-icon {
  width: 20px;
  flex-shrink: 0;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: #606266;
}
.menu-icon.recording {
  color: #f56c6c;
}
.menu-text {
  flex: 1;
}
.menu-text.recording {
  color: #f56c6c;
}
.menu-text.current {
  color: #409eff;
  font-weight: 600;
}
.menu-arrow {
  color: #909399;
  display: inline-flex;
  align-items: center;
}
.menu-state {
  color: #909399;
  font-size: 12px;
}
.check-icon {
  color: #409eff;
}
.panel-note {
  padding: 4px 10px 8px;
  color: #909399;
  font-size: 12px;
}
.view-only-checkbox {
  height: auto;
}
.gamepad-status {
  padding: 2px 10px 6px;
  color: #67c23a;
  font-size: 12px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
</style>
