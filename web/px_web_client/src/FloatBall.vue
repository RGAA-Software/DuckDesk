// 悬浮球 + 弹出菜单面板:对齐 C++ 端 float_controller.cpp(40x40 白球可拖,位置按比例持久化)
// 与 float_controller_panel.cpp(白色圆角卡片:顶部快捷按钮行 + 菜单项 + 右侧二级子面板)
// API 与旧 FloatToolbar.vue 完全兼容(props/models 不变),App.vue 只需换组件名
<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
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
  IconFileText,
  IconLanguage,
  IconPlugConnectedX,
} from '@tabler/icons-vue'
import { setAppLocale } from './locales/i18n'
import type { AppLocale } from './locales/index'
import type { PerfStats } from './rtc/stats'
import { SessionRecorder, recordFileName } from './rtc/recorder'
import { MSG_TYPE_CHANGE_MONITOR_RESOLUTION } from './rtc/proto'
import logoUrl from './assets/px_icon.png'
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

const props = withDefaults(
  defineProps<{
  connected: boolean
  // ft_data_channel 是否已就绪
  ftReady: boolean
  // 对端 FT 协议版本是否兼容(rustdesk 语义 = 2;旧版被控置灰入口)
  ftSupported: boolean
  // 性能采样数据(App.vue 每 2s 更新)
  perf: PerfStats
  // 最近一次收到的远端剪贴板文本
  remoteClipboard: string
  // 发送控制消息(px.Message 字段,camelCase),返回是否已发出
  send: (fields: Record<string, unknown>) => boolean
  // 「发送到远端」:读本地剪贴板并发往远端,返回是否成功
  sendClipboardToRemote: () => Promise<boolean>
  // 「复制到本地」:把 remoteClipboard 写入本地系统剪贴板
  copyRemoteToLocal: () => Promise<boolean>
  voiceCallSupported: boolean
  voiceCallPhase: 'idle' | 'outgoing' | 'connected' | 'error'
  voiceCallReason: string
  voiceCallRequiresHeadset: boolean
  voiceMicMuted: boolean
  voiceSpeakerMuted: boolean
  toggleVoiceCall: () => Promise<void>
  toggleVoiceMute: () => void
  toggleVoiceSpeakerMute: () => void
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
  virtualDisplayEnabled: boolean
  virtualDisplayOwnedCount: number
  virtualDisplayMaxCount: number
  virtualDisplayPending: boolean
  requestVirtualDisplay: (operation: 'create' | 'remove') => boolean
  remoteFps?: number
  clipboardAvailable?: boolean
  /** 是否可断开(连接中/已连接/重连中) */
  canDisconnect?: boolean
  /** 断开当前会话(App.vue disconnect) */
  disconnect: () => void
  log: (msg: string) => void
}>(),
  {
    remoteFps: 0,
    clipboardAvailable: false,
    canDisconnect: false,
  },
)

const { t, locale } = useI18n()

// 与 App 双向绑定的本地状态
const muted = defineModel<boolean>('muted', { required: true })
const viewOnly = defineModel<boolean>('viewOnly', { required: true })
const ftVisible = defineModel<boolean>('ftVisible', { required: true })
const perfVisible = defineModel<boolean>('perfVisible', { required: true })
const logVisible = defineModel<boolean>('logVisible', { required: true })

const voiceStateText = computed(() => {
  if (!props.voiceCallSupported) return props.voiceCallReason || t('float.voiceUnsupported')
  if (props.voiceCallPhase === 'outgoing') return t('float.voiceWaiting')
  if (props.voiceCallPhase === 'connected') return t('float.voiceConnected')
  if (props.voiceCallPhase === 'error') return props.voiceCallReason || t('float.voiceError')
  return ''
})

// ---------- 悬浮球位置(比例持久化)----------
const BALL_SIZE = 48
const LS_POS_KEY = 'px_web_client.float_ball_pos'
const pos = reactive({ x: 0, y: 0 })
const panelOpen = ref(false)
// 二级子面板:null=收起,'control'=控制,'display'=显示,'language'=语言
const subPanel = ref<'' | 'control' | 'display' | 'language'>('')
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
  viewport.w = window.innerWidth
  viewport.h = window.innerHeight
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
// 三级子面板(帧率/分辨率/切换显示器,同一时刻只渲染一个,共用一个 ref)
const subSubPanelRef = ref<HTMLElement | null>(null)

const PANEL_W = 250
const PANEL_GAP = 8
/** 面板距视口上下边的安全距离 */
const PANEL_SAFE = 16
/** 主面板估计高度(仅用于初始 top 防溢出) */
const PANEL_MAX_H = 440
/** 子菜单至少保留的可视高度;不够则整体上移 */
const SUB_PANEL_MIN_H = 180

// 视口尺寸(resize 时更新,驱动面板 maxHeight 重算)
const viewport = reactive({ w: window.innerWidth, h: window.innerHeight })

// 子菜单顶部对齐触发 item(水平对齐),而非主面板顶部
const subPanelAnchorTop = ref(0)
const subSubPanelAnchorTop = ref(0)

/** 飞出菜单定位:优先与 item 顶对齐;底部留安全距;空间不足时上移并限制 maxHeight 滚动 */
function placeFlyout(preferredTop: number, minH = SUB_PANEL_MIN_H): { top: number; maxHeight: number } {
  const vh = viewport.h
  let top = Math.max(PANEL_SAFE, preferredTop)
  let maxHeight = vh - top - PANEL_SAFE
  if (maxHeight < minH) {
    top = Math.max(PANEL_SAFE, vh - PANEL_SAFE - minH)
    maxHeight = vh - top - PANEL_SAFE
  }
  maxHeight = Math.max(120, maxHeight)
  return { top, maxHeight }
}

function captureAnchorTop(ev?: Event): number {
  const el = ev?.currentTarget as HTMLElement | null | undefined
  if (el?.getBoundingClientRect) return el.getBoundingClientRect().top
  return parseFloat(panelStyle.value.top) || PANEL_SAFE
}

function togglePanel() {
  panelOpen.value = !panelOpen.value
  if (!panelOpen.value) {
    subPanel.value = ''
    subSubPanel.value = ''
    subPanelAnchorTop.value = 0
    subSubPanelAnchorTop.value = 0
  }
}

function closePanel() {
  panelOpen.value = false
  subPanel.value = ''
  subSubPanel.value = ''
  subPanelAnchorTop.value = 0
  subSubPanelAnchorTop.value = 0
}

// 球在右半屏时面板弹左侧,否则右侧
const panelOnLeft = computed(() => pos.x + BALL_SIZE / 2 > window.innerWidth / 2)

const panelStyle = computed(() => {
  const vw = viewport.w
  const vh = viewport.h
  let left = panelOnLeft.value ? pos.x - PANEL_GAP - PANEL_W : pos.x + BALL_SIZE + PANEL_GAP
  left = Math.min(Math.max(PANEL_SAFE, left), vw - PANEL_W - PANEL_SAFE)
  let top = pos.y + BALL_SIZE / 2 - 60
  top = Math.min(Math.max(PANEL_SAFE, top), vh - PANEL_MAX_H - PANEL_SAFE)
  const maxHeight = Math.max(120, vh - top - PANEL_SAFE)
  return { left: `${left}px`, top: `${top}px`, width: `${PANEL_W}px`, maxHeight: `${maxHeight}px` }
})

// 二级子面板:从主面板侧缘弹出,顶部与点击的 menu-item 对齐;底部安全距内滚动
const subPanelStyle = computed(() => {
  const vw = viewport.w
  const mainLeft = parseFloat(panelStyle.value.left)
  let left = panelOnLeft.value ? mainLeft - PANEL_GAP - PANEL_W : mainLeft + PANEL_W + PANEL_GAP
  left = Math.min(Math.max(PANEL_SAFE, left), vw - PANEL_W - PANEL_SAFE)
  const { top, maxHeight } = placeFlyout(
    subPanelAnchorTop.value || parseFloat(panelStyle.value.top),
  )
  return { left: `${left}px`, top: `${top}px`, width: `${PANEL_W}px`, maxHeight: `${maxHeight}px` }
})

// 三级子面板:从二级面板侧缘继续弹出,顶部与二级里点击的 item 对齐;底部安全距内滚动
const subSubPanelStyle = computed(() => {
  const vw = viewport.w
  const subLeft = parseFloat(subPanelStyle.value.left)
  let left = panelOnLeft.value ? subLeft - PANEL_GAP - PANEL_W : subLeft + PANEL_W + PANEL_GAP
  left = Math.min(Math.max(PANEL_SAFE, left), vw - PANEL_W - PANEL_SAFE)
  const { top, maxHeight } = placeFlyout(
    subSubPanelAnchorTop.value || subPanelAnchorTop.value || parseFloat(panelStyle.value.top),
  )
  return { left: `${left}px`, top: `${top}px`, width: `${PANEL_W}px`, maxHeight: `${maxHeight}px` }
})

// 点面板外收起(全局 pointerdown)
function onGlobalPointerDown(ev: PointerEvent) {
  if (!panelOpen.value) return
  const t = ev.target as Node
  if (ballRef.value?.contains(t)) return
  if (panelRef.value?.contains(t)) return
  if (subPanelRef.value?.contains(t)) return
  // 三级面板也必须排除,否则 pointerdown 先收起面板、click 永远到不了按钮
  if (subSubPanelRef.value?.contains(t)) return
  closePanel()
}

function toggleSubPanel(name: 'control' | 'display' | 'language', ev?: Event) {
  if (subPanel.value === name) {
    subPanel.value = ''
    subSubPanel.value = ''
    return
  }
  subPanelAnchorTop.value = captureAnchorTop(ev)
  subPanel.value = name
  subSubPanel.value = ''
  subSubPanelAnchorTop.value = 0
  // 打开后按实际 DOM 再校准一次(避免事件目标不是按钮本身)
  void nextTick(() => {
    const open = panelRef.value?.querySelector('.menu-item.open') as HTMLElement | null
    if (open) subPanelAnchorTop.value = open.getBoundingClientRect().top
  })
}

function toggleSubSubPanel(name: 'fps' | 'resolution' | 'monitor', ev?: Event) {
  if (subSubPanel.value === name) {
    subSubPanel.value = ''
    return
  }
  subSubPanelAnchorTop.value = captureAnchorTop(ev)
  subSubPanel.value = name
  void nextTick(() => {
    const open = subPanelRef.value?.querySelector('.menu-item.open') as HTMLElement | null
    if (open) subSubPanelAnchorTop.value = open.getBoundingClientRect().top
  })
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
    ElMessage.warning(t('float.pipUnsupported'))
    return false
  }
  try {
    if (document.pictureInPictureElement) {
      await document.exitPictureInPicture()
    } else {
      // 全屏与 PiP 互斥:先退全屏再进 PiP
      if (document.fullscreenElement) {
        await document.exitFullscreen()
        props.log(t('float.pipExitFullscreen'))
      }
      await v.requestPictureInPicture()
    }
    pipActive.value = !!document.pictureInPictureElement
    return pipActive.value
  } catch (err) {
    ElMessage.warning(t('float.pipFail', { err: err instanceof Error ? err.message : String(err) }))
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
    ElMessage.warning(t('float.recordUnsupported'))
    return false
  }
  const stream = props.getVideo()?.srcObject as MediaStream | null
  if (!stream) {
    ElMessage.warning(t('float.recordNoStream'))
    return false
  }
  try {
    recorder.start(stream)
  } catch (err) {
    ElMessage.warning(t('float.recordStartFail', { err: err instanceof Error ? err.message : String(err) }))
    return false
  }
  recording.value = true
  recordSeconds.value = 0
  recordTimer = window.setInterval(() => {
    recordSeconds.value += 1
  }, 1000)
  props.log(t('float.recordStarted', { mime: recorder.mimeType || t('float.recordDefaultMime') }))
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
    props.log(
      t('float.recordDone', {
        size: (r.blob.size / 1024).toFixed(1),
        seconds: r.seconds.toFixed(1),
        mime: r.mimeType,
      }),
    )
    return { size: r.blob.size, mimeType: r.mimeType }
  } catch (err) {
    recording.value = false
    ElMessage.warning(t('float.recordStopFail', { err: err instanceof Error ? err.message : String(err) }))
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
    ElMessage.warning(t('float.lockMouseFail', { err: err instanceof Error ? err.message : String(err) }))
  }
}

// ---------- 控制消息(走 media_data_channel)----------
function emit(fields: Record<string, unknown>, desc: string) {
  if (props.send(fields)) {
    props.log(t('float.ctrlSent', { desc }))
  } else {
    ElMessage.warning(t('float.dcNotReady'))
  }
}

function sendCtrlAltDelete() {
  // 对齐 ProtoMessageMaker::MakeCtrlAltDelete(type=330,空 ReqCtrlAltDelete 子消息)
  emit({ type: MSG_TYPE_REQ_CTRL_ALT_DELETE, reqCtrlAltDelete: {} }, t('float.ctrlAltDel'))
}

function hardUpdate() {
  // 对齐 BaseWorkspace::SendHardUpdateDesktopMessage(type=341,无子消息)
  emit({ type: MSG_TYPE_HARD_UPDATE_DESKTOP }, t('float.refreshDesc'))
}

function lockDevice() {
  // 对齐 ProtoMessageMaker::MakeLockDevice(type=328,空 LockDevice 子消息)
  emit({ type: MSG_TYPE_LOCK_DEVICE, lockDevice: {} }, t('float.lockDesc'))
}

async function stopRender() {
  try {
    await ElMessageBox.confirm(t('float.restartConfirm'), t('float.restartConfirmTitle'), {
      type: 'warning',
      confirmButtonText: t('float.restart'),
      cancelButtonText: t('float.cancel'),
    })
  } catch {
    return
  }
  // 对齐 ProtoMessageMaker::MakeStopRender(type=329,空 StopRender 子消息)
  emit({ type: MSG_TYPE_STOP_RENDER, stopRender: {} }, t('float.restartDesc'))
}

// ---------- 显示子面板:帧率/分辨率/切换显示器/锁定鼠标/麦克风/手柄 ----------
const fps = ref(0) // 0 = 未知(尚未由本端设置)
const FPS_OPTIONS = [15, 30, 60, 90, 120, 144]

// 对齐 Windows:用宿主上报的当前 fps 作为选中项;本端改帧率后以本地选择为准,
// 仅在 remoteFps 变化(收到新配置)时同步。
watch(
  () => props.remoteFps,
  (r) => {
    if (r > 0) fps.value = r
  },
  { immediate: true },
)

function modifyFps(value: number) {
  // 对齐 BaseWorkspace::SendModifyFpsMessage(type=480,ModifyFps{fps})
  emit({ type: MSG_TYPE_MODIFY_FPS, modifyFps: { fps: value } }, t('float.modifyFps', { n: value }))
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
    t('float.modifyRes', { w, h, name: name || t('float.defaultMonitor') }),
  )
  subSubPanel.value = ''
}

// 切换显示器:列表来自 kServerConfiguration(monitors prop),不再走 /get/render/configuration
function sendSwitchMonitor(name: string) {
  // 对齐 BaseWorkspace::SendSwitchMonitorMessage(type=170,SwitchMonitor{name})
  emit({ type: MSG_TYPE_SWITCH_MONITOR, switchMonitor: { name } }, t('float.switchMonitorDesc', { name }))
  subSubPanel.value = ''
}

// ---------- 剪贴板 ----------
async function onSendClipboard() {
  if (!(await props.sendClipboardToRemote())) {
    ElMessage.warning(t('clipboard.sendFail'))
  }
}

async function onCopyRemote() {
  if (await props.copyRemoteToLocal()) {
    ElMessage.success(t('clipboard.copyOk'))
  } else {
    ElMessage.warning(t('clipboard.copyFail'))
  }
}

// ---------- 菜单动作 ----------
// ---------- 菜单动作 ----------
function openFileTransfer() {
  if (!props.ftReady || !props.ftSupported) return
  ftVisible.value = true
  closePanel()
}

function togglePerf() {
  perfVisible.value = !perfVisible.value
}

function toggleLog() {
  logVisible.value = !logVisible.value
}

function pickLocale(loc: AppLocale) {
  setAppLocale(loc)
  subPanel.value = ''
}

function onDisconnect() {
  closePanel()
  props.disconnect()
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
    :title="t('float.panelTitle')"
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
      <button class="quick-btn" :title="muted ? t('float.unmute') : t('float.mute')" @click="toggleMute">
        <IconVolumeOff v-if="muted" :size="20" />
        <IconVolume v-else :size="20" />
      </button>
      <button class="quick-btn" :title="isFullscreen ? t('float.exitFullscreen') : t('float.fullscreen')" @click="toggleFullscreen">
        <IconMinimize v-if="isFullscreen" :size="20" />
        <IconMaximize v-else :size="20" />
      </button>
      <button
        class="quick-btn"
        :class="{ active: pipActive }"
        :disabled="!connected"
        :title="t('float.pip')"
        @click="togglePip"
      >
        <IconPictureInPicture :size="20" />
      </button>
    </div>

    <!-- 菜单列表 -->
    <div class="menu">
      <button class="menu-item" :class="{ open: subPanel === 'control' }" @click="toggleSubPanel('control', $event)">
        <span class="menu-icon"><IconSettings :size="17" /></span>
        <span class="menu-text">{{ t('float.control') }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button data-testid="display-submenu-toggle" class="menu-item" :class="{ open: subPanel === 'display' }" @click="toggleSubPanel('display', $event)">
        <span class="menu-icon"><IconDeviceDesktop :size="17" /></span>
        <span class="menu-text">{{ t('float.display') }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <div class="menu-item static virtual-display-row">
        <span class="menu-icon"><IconDeviceDesktop :size="17" /></span>
        <span class="menu-text">
          {{ t('float.virtualDisplay') }}
          ({{ virtualDisplayOwnedCount }}/{{ virtualDisplayMaxCount || 8 }})
        </span>
        <span class="virtual-display-actions">
          <button
            data-testid="virtual-display-remove"
            type="button"
            :title="t('float.virtualDisplayRemove')"
            :disabled="!connected || !virtualDisplayEnabled || virtualDisplayPending || virtualDisplayOwnedCount <= 0"
            @click.stop="requestVirtualDisplay('remove')"
          >−</button>
          <button
            data-testid="virtual-display-add"
            type="button"
            :title="t('float.virtualDisplayAdd')"
            :disabled="!connected || !virtualDisplayEnabled || virtualDisplayPending || virtualDisplayOwnedCount >= (virtualDisplayMaxCount || 8)"
            @click.stop="requestVirtualDisplay('create')"
          >+</button>
        </span>
      </div>
      <button
        data-testid="voice-call-toggle"
        class="menu-item"
        :class="{ danger: voiceCallPhase === 'connected' }"
        :disabled="!connected || !voiceCallSupported"
        :title="voiceCallReason || (voiceCallRequiresHeadset ? t('float.voiceHeadsetHint') : '')"
        @click="props.toggleVoiceCall"
      >
        <span class="menu-icon"><IconMicrophone :size="17" /></span>
        <span class="menu-text">
          {{ voiceCallPhase === 'connected' ? t('float.voiceHangUp') : t('float.voiceCall') }}
        </span>
        <span class="menu-state">{{ voiceStateText }}</span>
      </button>
      <button
        class="menu-item"
        :disabled="!ftReady || !ftSupported"
        :title="!ftSupported && connected ? t('float.ftNotSupported') : ''"
        @click="openFileTransfer"
      >
        <span class="menu-icon"><IconTransfer :size="17" /></span>
        <span class="menu-text">{{ t('float.fileTransfer') }}</span>
        <span class="menu-state">
          {{ ftSupported ? (ftReady ? '' : t('float.notReady')) : t('float.ftNotSupported') }}
        </span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="toggleRecord">
        <span class="menu-icon" :class="{ recording }"><IconPlayerRecord :size="17" /></span>
        <span class="menu-text" :class="{ recording }">
          {{ recording ? t('float.stopRecord', { time: recordTimeText }) : t('float.record') }}
        </span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="togglePerf">
        <span class="menu-icon"><IconChartBar :size="17" /></span>
        <span class="menu-text">{{ t('float.stats') }}</span>
        <span class="menu-state">{{ perfVisible ? t('float.on') : '' }}</span>
      </button>
      <button class="menu-item" @click="toggleLog">
        <span class="menu-icon"><IconFileText :size="17" /></span>
        <span class="menu-text">{{ t('float.logs') }}</span>
        <span class="menu-state">{{ logVisible ? t('float.on') : '' }}</span>
      </button>
      <button class="menu-item" :class="{ open: subPanel === 'language' }" @click="toggleSubPanel('language', $event)">
        <span class="menu-icon"><IconLanguage :size="17" /></span>
        <span class="menu-text">{{ t('lang.label') }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button
        class="menu-item danger"
        :disabled="!props.canDisconnect"
        @click="onDisconnect"
      >
        <span class="menu-icon"><IconPlugConnectedX :size="17" /></span>
        <span class="menu-text">{{ t('float.disconnect') }}</span>
      </button>
    </div>
  </div>

  <!-- 二级子面板:语言 -->
  <div v-if="panelOpen && subPanel === 'language'" ref="subPanelRef" class="ball-panel sub" :style="subPanelStyle">
    <div class="menu">
      <button class="menu-item" @click="pickLocale('zh')">
        <span class="menu-icon check-icon"><IconCheck v-if="locale === 'zh'" :size="16" /></span>
        <span class="menu-text" :class="{ current: locale === 'zh' }">{{ t('lang.zh') }}</span>
      </button>
      <button class="menu-item" @click="pickLocale('en')">
        <span class="menu-icon check-icon"><IconCheck v-if="locale === 'en'" :size="16" /></span>
        <span class="menu-text" :class="{ current: locale === 'en' }">{{ t('lang.en') }}</span>
      </button>
    </div>
  </div>

  <!-- 二级子面板:控制 -->
  <div v-if="panelOpen && subPanel === 'control'" ref="subPanelRef" class="ball-panel sub" :style="subPanelStyle">
    <div class="menu">
      <button
        v-if="props.clipboardAvailable"
        class="menu-item"
        :disabled="!connected"
        @click="onSendClipboard"
      >
        <span class="menu-icon"><IconSend :size="16" /></span>
        <span class="menu-text">{{ t('clipboard.sendRemote') }}</span>
      </button>
      <button
        v-if="props.clipboardAvailable"
        class="menu-item"
        :disabled="!remoteClipboard"
        @click="onCopyRemote"
      >
        <span class="menu-icon"><IconClipboardCopy :size="16" /></span>
        <span class="menu-text">{{ t('clipboard.copyLocal') }}</span>
      </button>
      <div class="menu-item static">
        <el-checkbox v-model="viewOnly" class="view-only-checkbox">{{ t('float.viewOnly') }}</el-checkbox>
      </div>
      <button class="menu-item" :disabled="!connected" @click="sendCtrlAltDelete">
        <span class="menu-icon"><IconKeyboard :size="16" /></span>
        <span class="menu-text">{{ t('float.ctrlAltDel') }}</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="hardUpdate">
        <span class="menu-icon"><IconRefresh :size="16" /></span>
        <span class="menu-text">{{ t('float.refreshScreen') }}</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="lockDevice">
        <span class="menu-icon"><IconLock :size="16" /></span>
        <span class="menu-text">{{ t('float.lockDevice') }}</span>
      </button>
      <button class="menu-item danger" :disabled="!connected" @click="stopRender">
        <span class="menu-icon"><IconRestore :size="16" /></span>
        <span class="menu-text">{{ t('float.restartRender') }}</span>
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
        @click="toggleSubSubPanel('fps', $event)"
      >
        <span class="menu-icon"><IconGauge :size="16" /></span>
        <span class="menu-text">{{ t('float.fps') }}</span>
        <span class="menu-state">{{ fps > 0 ? fps : '' }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button
        class="menu-item"
        :class="{ open: subSubPanel === 'resolution' }"
        :disabled="!connected"
        @click="toggleSubSubPanel('resolution', $event)"
      >
        <span class="menu-icon"><IconAspectRatio :size="16" /></span>
        <span class="menu-text">{{ t('float.resolution') }}</span>
        <span class="menu-state">
          {{ currentResolution ? `${currentResolution.width}×${currentResolution.height}` : '' }}
        </span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button
        class="menu-item"
        :class="{ open: subSubPanel === 'monitor' }"
        :disabled="!connected"
        @click="toggleSubSubPanel('monitor', $event)"
      >
        <span class="menu-icon"><IconDevices :size="16" /></span>
        <span class="menu-text">{{ t('float.switchMonitor') }}</span>
        <span class="menu-state">{{ capturingMonitor }}</span>
        <span class="menu-arrow"><IconChevronRight :size="15" /></span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="togglePointerLock">
        <span class="menu-icon"><IconMouse :size="16" /></span>
        <span class="menu-text">{{ t('float.lockMouse') }}</span>
        <span class="menu-state">{{ pointerLocked ? t('float.locked') : '' }}</span>
      </button>
      <button
        data-testid="voice-microphone-mute"
        class="menu-item"
        :disabled="voiceCallPhase !== 'connected'"
        @click="props.toggleVoiceMute"
      >
        <span class="menu-icon"><IconMicrophone :size="16" /></span>
        <span class="menu-text">{{ t('float.voiceMute') }}</span>
        <span class="menu-state">{{ voiceMicMuted ? t('float.on') : t('float.off') }}</span>
      </button>
      <button
        data-testid="voice-speaker-mute"
        class="menu-item"
        :disabled="voiceCallPhase !== 'connected'"
        @click="props.toggleVoiceSpeakerMute"
      >
        <span class="menu-icon">
          <IconVolumeOff v-if="voiceSpeakerMuted" :size="16" />
          <IconVolume v-else :size="16" />
        </span>
        <span class="menu-text">{{ t('float.voiceSpeakerMute') }}</span>
        <span class="menu-state">{{ voiceSpeakerMuted ? t('float.on') : t('float.off') }}</span>
      </button>
      <button class="menu-item" :disabled="!connected" @click="props.toggleGamepad">
        <span class="menu-icon"><IconDeviceGamepad2 :size="16" /></span>
        <span class="menu-text">{{ t('float.gamepad') }}</span>
        <span class="menu-state">{{ gamepadOn ? t('float.on') : t('float.off') }}</span>
      </button>
      <div v-if="gamepadOn" class="gamepad-status" :title="props.gamepadStatus">
        {{ props.gamepadStatus || t('float.detecting') }}
      </div>
    </div>
  </div>

  <!-- 三级子面板:帧率 -->
  <div
    v-if="panelOpen && subPanel === 'display' && subSubPanel === 'fps'"
    ref="subSubPanelRef"
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
        <span class="menu-text" :class="{ current: fps === f }">{{ t('float.fpsUnit', { n: f }) }}</span>
      </button>
    </div>
  </div>

  <!-- 三级子面板:分辨率 -->
  <div
    v-if="panelOpen && subPanel === 'display' && subSubPanel === 'resolution'"
    ref="subSubPanelRef"
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
        {{ t('float.noResolutionList') }}
      </div>
    </div>
  </div>

  <!-- 三级子面板:切换显示器(列表来自 kServerConfiguration) -->
  <div
    v-if="panelOpen && subPanel === 'display' && subSubPanel === 'monitor'"
    ref="subSubPanelRef"
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
          <template v-if="m.primary"> · {{ t('float.primary') }}</template>
        </span>
      </button>
      <div v-if="!monitors.length" class="panel-note">{{ t('float.noMonitorList') }}</div>
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
  /* max-height 由 style 按 top+底部安全距动态设置,过长内容在此滚动 */
  overflow-y: auto;
  overflow-x: hidden;
  overscroll-behavior: contain;
  z-index: 59;
  padding: 6px;
  box-sizing: border-box;
}
.ball-panel::-webkit-scrollbar {
  width: 6px;
}
.ball-panel::-webkit-scrollbar-thumb {
  background: #c0c4cc;
  border-radius: 3px;
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
.menu-item.danger .menu-text,
.menu-item.danger .menu-icon {
  color: #f56c6c;
}
.menu-item.danger:hover:not(:disabled) {
  background: #fef0f0;
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
.virtual-display-row .menu-text {
  white-space: nowrap;
}
.virtual-display-actions {
  display: inline-flex;
  gap: 4px;
  margin-left: auto;
}
.virtual-display-actions button {
  width: 27px;
  height: 24px;
  padding: 0;
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  background: #fff;
  color: #303133;
  font-size: 17px;
  line-height: 20px;
  cursor: pointer;
}
.virtual-display-actions button:hover:not(:disabled) {
  color: #409eff;
  border-color: #79bbff;
  background: #ecf5ff;
}
.virtual-display-actions button:disabled {
  opacity: 0.45;
  cursor: default;
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
