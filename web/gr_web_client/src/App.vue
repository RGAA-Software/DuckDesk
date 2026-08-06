<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import CryptoJS from 'crypto-js'
import { InputController } from './rtc/input'
import { sendControlMessage } from './rtc/control'
import { GamepadController } from './rtc/gamepad'
import type { GamepadSnapshot } from './rtc/gamepad'
import { sha256Hex } from './rtc/file_transfer'
import { PerfCollector, EMPTY_PERF, perfSummaryLine } from './rtc/stats'
import type { PerfStats } from './rtc/stats'
import { sendClipboardText, parseClipboardText } from './rtc/clipboard'
import { decodeMessage } from './rtc/proto'
import {
  MSG_TYPE_HELLO,
  MSG_TYPE_CLIPBOARD_INFO,
  MSG_TYPE_SERVER_CONFIGURATION,
  MSG_TYPE_MONITOR_SWITCHED,
  MSG_TYPE_CHANGE_MONITOR_RESOLUTION_RESULT,
  MSG_TYPE_SWITCH_FULL_COLOR_MODE,
  MSG_TYPE_VIDEO_CODEC_CHANGED,
} from './rtc/proto'
import { TlvReassembler } from './rtc/tlv'
import { ElMessage, ElMessageBox } from 'element-plus'
import FloatBall from './FloatBall.vue'
import FileTransferWindow from './FileTransferWindow.vue'
import { useFileTransfer } from './useFileTransfer'

// ---------- 信令契约(对齐 render net_ws http_handler.cpp)----------
// POST /alloc/local/rtc?device_id=X&stream_id=Y&safety_pwd_md5=md5(安全密码或临时密码)[&takeover=1]
// 请求体: { "sdp": "<offer>" }
// 响应: { "code":200, "message":"ok", "data": { "answer_sdp": "..." } }
// code=704(kHandlerErrRtcLocalOccupied): 同 stream_id 已有活跃连接,需用户确认后带 takeover=1 重试
const SIGNAL_URL = '/alloc/local/rtc'
const SIGNAL_CODE_OCCUPIED = 704
const DATA_CHANNEL_LABEL = 'media_data_channel' // render 端按此名字识别(rtc_server.cpp:81)
const FT_DATA_CHANNEL_LABEL = 'ft_data_channel' // 文件传输通道(rtc_server.cpp:90)
const INPUT_DATA_CHANNEL_LABEL = 'input_data_channel' // 输入专用不可靠通道(render 端按此名字识别)
const PING_DATA_CHANNEL_LABEL = 'ping_data_channel' // 诊断通道:render 收到即回显,实测 datachannel RTT
const ICE_GATHER_TIMEOUT_MS = 10000

type ConnStatus = 'idle' | 'connecting' | 'connected' | 'failed' | 'reconnecting'

// ---------- 会话恢复/自动重连 ----------
// disconnected/failed/closed(非手动断开)时等待 3s 走完整重连,最多自动重试 3 次
const MAX_AUTO_RECONNECT = 3
const RECONNECT_DELAY_MS = 3000
const reconnectCount = ref(0)
// 手动「断开」置位,阻止 onconnectionstatechange 里的 closed 触发自动重连
let manualClose = false
let reconnectTimer: number | null = null
// localStorage 记忆上次连接的 deviceId/streamId(不存密码)
const LS_LAST_CONN = 'gr_web_client.last_conn'

// 连接看门狗:Chrome 下本地 pc.close() 不会触发 connectionstatechange(实测),
// 2s 轮询兜底,保证异常关闭也能进入自动重连
let connWatchdog: number | null = null

function startConnWatchdog() {
  stopConnWatchdog()
  connWatchdog = window.setInterval(() => {
    if (!pc) return
    const s = pc.connectionState
    if (s === 'closed' || s === 'failed') {
      addLog(`看门狗发现连接异常: ${s}`)
      scheduleReconnect(s)
    }
  }, 2000)
}

function stopConnWatchdog() {
  if (connWatchdog !== null) {
    window.clearInterval(connWatchdog)
    connWatchdog = null
  }
}

function cancelReconnectTimer() {
  if (reconnectTimer !== null) {
    window.clearTimeout(reconnectTimer)
    reconnectTimer = null
  }
}

function scheduleReconnect(reason: string) {
  if (manualClose || reconnectTimer !== null) return
  if (reconnectCount.value >= MAX_AUTO_RECONNECT) {
    status.value = 'failed'
    errorMsg.value = `连接${reason},自动重连 ${MAX_AUTO_RECONNECT} 次后仍失败`
    addLog(errorMsg.value)
    return
  }
  reconnectCount.value += 1
  status.value = 'reconnecting'
  errorMsg.value = ''
  addLog(
    `连接${reason},${RECONNECT_DELAY_MS / 1000}s 后自动重连(第 ${reconnectCount.value}/${MAX_AUTO_RECONNECT} 次)`,
  )
  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = null
    void connect()
  }, RECONNECT_DELAY_MS)
}

// 指针锁定(相对鼠标模式)状态,watch 同步给 InputController
const pointerLocked = ref(false)

function onPointerLockChange() {
  const locked = document.pointerLockElement !== null && document.pointerLockElement === videoRef.value
  pointerLocked.value = locked
  addLog(locked ? '鼠标已锁定(相对模式,Esc 退出)' : '鼠标锁定已解除')
}

const form = reactive({
  deviceId: '',
  streamId: '',
  password: '',
})

// 流 ID 固定由设备 ID 派生,不允许随意填写:同一台设备的 stream_id 恒定,
// 新连接会在 render 端顶掉同 stream_id 的旧连接(单路独占)。
// 设备 ID 变化时自动联动更新。
watch(() => form.deviceId, (id) => {
  form.streamId = id ? `web_${id}` : ''
}, { immediate: true })

const status = ref<ConnStatus>('idle')
const errorMsg = ref('')
const logs = ref<string[]>([])
const videoRef = ref<HTMLVideoElement | null>(null)
const hasVideo = ref(false)
// 仅观看:勾选后不回传鼠标键盘输入(开关在悬浮工具条内)
const viewOnly = ref(false)
// 声音:默认静音以保证自动播放,工具条内可开关
const muted = ref(true)
// 麦克风上行:建连时用 addTransceiver 占位(sendrecv),开/关 mic 只 replaceTrack,
// 不触发重新协商(render 侧 RtcServer 只处理一次 offer/answer)
const micOn = ref(false)
let micTransceiver: RTCRtpTransceiver | null = null
let micStream: MediaStream | null = null

async function toggleMic() {
  if (micOn.value) {
    if (micTransceiver) {
      try { await micTransceiver.sender.replaceTrack(null) } catch { /* ignore */ }
    }
    micStream?.getTracks().forEach((t) => t.stop())
    micStream = null
    micOn.value = false
    addLog('麦克风已关闭')
    return
  }
  if (!pc || !micTransceiver) {
    addLog('尚未连接,无法开启麦克风')
    return
  }
  try {
    micStream = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: true },
    })
    const track = micStream.getAudioTracks()[0]
    if (!track) throw new Error('未获取到音频轨')
    await micTransceiver.sender.replaceTrack(track)
    micOn.value = true
    addLog(`麦克风已开启(上行到远端扬声器): ${track.label || 'audio'}`)
  } catch (err) {
    addLog(`开启麦克风失败: ${String(err)}`)
    micStream?.getTracks().forEach((t) => t.stop())
    micStream = null
    micOn.value = false
  }
}

// 悬浮工具条的控制消息发送入口(走 media_data_channel)
function sendControl(fields: Record<string, unknown>): boolean {
  return sendControlMessage(dc, form.deviceId, form.streamId, fields)
}

// ---------- 游戏手柄(Gamepad API -> kGamepadState -> render ViGEm 虚拟手柄)----------
const gamepadOn = ref(false)
const gamepadStatus = ref('')
let gamepad: GamepadController | null = null

function getGamepad(): GamepadController {
  if (!gamepad) {
    gamepad = new GamepadController({
      send: sendControl,
      onLog: addLog,
      onStatus: (text) => {
        gamepadStatus.value = text
      },
    })
  }
  return gamepad
}

function toggleGamepad() {
  const gc = getGamepad()
  if (gamepadOn.value) {
    gc.disable()
    gamepadOn.value = false
    return
  }
  if (gc.enable()) {
    gamepadOn.value = true
  }
}

let pc: RTCPeerConnection | null = null
let dc: RTCDataChannel | null = null
let inputDc: RTCDataChannel | null = null
let pingDc: RTCDataChannel | null = null
let pingTimer: number | null = null
const pingRttMs = ref(-1)
let input: InputController | null = null

// ---------- 性能面板(pc.getStats 采样)----------
const perf = ref<PerfStats>({ ...EMPTY_PERF })
// ?debug=1 打开页面即展开性能面板(诊断用)
const perfVisible = ref(new URLSearchParams(window.location.search).get('debug') === '1')
// 输入通道诊断的上一采样基准(算速率用)
let lastInputSampleAt = 0
let lastDomMoves = 0
let lastInputSent = 0
const perfCollector = new PerfCollector((s) => {
  perf.value = s
  // 每个采样周期(2s)写一条紧凑诊断到日志面板,用户可直接复制发回;
  // 附加输入通道指标:datachannel RTT、DOM 鼠标事件速率、实际发送速率、发送缓冲
  const now = Date.now()
  let extra = ` input_rtt=${pingRttMs.value >= 0 ? pingRttMs.value.toFixed(1) : '-'}ms buf=${inputDc?.bufferedAmount ?? 0}B`
  if (lastInputSampleAt > 0 && now > lastInputSampleAt) {
    const dt = (now - lastInputSampleAt) / 1000
    if (input) {
      extra += ` dom_ev=${((input.domMoveEvents - lastDomMoves) / dt).toFixed(0)}/s sent=${((input.sentMessages - lastInputSent) / dt).toFixed(0)}/s`
    }
    // 旧版 render 经 datachannel 灌视频帧时被本端丢弃的速率(应为 0)
    extra += ` dc_media_drops=${(dcMediaDrops / dt).toFixed(0)}/s`
    dcMediaDrops = 0
  }
  lastInputSampleAt = now
  if (input) {
    lastDomMoves = input.domMoveEvents
    lastInputSent = input.sentMessages
  }
  addLog(perfSummaryLine(s) + extra)
})

// 性能面板显示值(码率单位为 kbps,>=1000 转 Mbps)
const perfBitrateText = computed(() => {
  const kbps = perf.value.videoBitrateKbps
  return kbps >= 1000 ? `${(kbps / 1000).toFixed(2)} Mbps` : `${kbps.toFixed(0)} kbps`
})
const perfResolutionText = computed(() =>
  perf.value.width > 0 ? `${perf.value.width}×${perf.value.height}` : '-',
)
const perfLossText = computed(() => `${(perf.value.lossRate * 100).toFixed(1)}%`)
const perfRttText = computed(() => (perf.value.rttMs > 0 ? `${perf.value.rttMs.toFixed(0)} ms` : '-'))
const perfJitterText = computed(() => `${perf.value.jitterMs.toFixed(1)} ms`)

// 底部日志面板:默认收起,顶部控制条「日志」按钮切换
const logVisible = ref(false)

// ---------- 剪贴板文本同步(kClipboardInfo,双向)----------
// 远端(render 机器)最近一次广播的剪贴板文本
const remoteClipboard = ref('')
// media_data_channel 接收侧 TLV 重组(>128KB 消息 render 会分片)
const dcReassembler = new TlvReassembler()

// ---------- 远端显示器配置(kServerConfiguration,含可用分辨率列表)----------
// dc 打开后本端发 kHello,render 回推 config(rd_app.cpp:SendConfigurationBack)
interface RemoteMonitor {
  name: string
  resolutions: Array<{ width: number; height: number }>
  currentWidth: number
  currentHeight: number
  primary: boolean
}
const remoteMonitors = ref<RemoteMonitor[]>([])
const capturingMonitor = ref('')

// render 修复前(旧版本插件)会把每个编码视频帧也塞进 media_data_channel
// (tc.Message type=kVideoFrame(30)/kAudioFrame(40), ~20KB×60fps),web 端不认识,
// 但逐帧 proto 解码会淹掉主线程。wire 级窥探 type 字段直接丢弃并计数。
let dcMediaDrops = 0
function isMediaFramePayload(p: Uint8Array): boolean {
  if (p.length < 2 || p[0] !== 0x08) return false // field 1 (type), varint
  let type = 0
  let shift = 0
  let i = 1
  while (i < p.length && i < 11) {
    const b = p[i++]
    type |= (b & 0x7f) << shift
    if (!(b & 0x80)) break
    shift += 7
  }
  return type === 30 || type === 40
}

function handleDcBinary(buf: ArrayBuffer) {
  for (const payload of dcReassembler.feed(buf)) {
    if (isMediaFramePayload(payload)) {
      dcMediaDrops++
      continue
    }
    let msg: ReturnType<typeof decodeMessage> | null = null
    try {
      msg = decodeMessage(payload)
    } catch {
      continue
    }
    if (msg.type === MSG_TYPE_CLIPBOARD_INFO) {
      const text = parseClipboardText(payload)
      if (text !== null) {
        remoteClipboard.value = text
        addLog(`收到远端剪贴板文本 (${text.length} 字符): ${text.slice(0, 80)}`)
      }
    } else if (msg.type === MSG_TYPE_SERVER_CONFIGURATION && msg.config) {
      const cfg = msg.config
      remoteMonitors.value = (cfg.monitorsInfo ?? []).map((m) => ({
        name: m.name,
        resolutions: (m.resolutions ?? []).map((r) => ({ width: r.width, height: r.height })),
        currentWidth: m.currentWidth,
        currentHeight: m.currentHeight,
        primary: m.primary,
      }))
      capturingMonitor.value = cfg.capturingMonitorName ?? ''
      addLog(`收到远端显示器配置: ${remoteMonitors.value.length} 个显示器, 采集 ${capturingMonitor.value}`)
    } else if (msg.type === MSG_TYPE_MONITOR_SWITCHED && msg.monitorSwitched) {
      // 切屏回包:更新当前采集显示器与输入回放坐标系(否则鼠标仍按旧屏几何映射)
      const name = msg.monitorSwitched.name
      capturingMonitor.value = name
      input?.setMonitorName(name)
      addLog(`采集显示器已切换 -> ${name}`)
    } else if (msg.type === MSG_TYPE_CHANGE_MONITOR_RESOLUTION_RESULT && msg.changeMonitorResolutionResult) {
      const r = msg.changeMonitorResolutionResult
      if (r.result) {
        ElMessage.success(`分辨率已切换 (${r.monitorName})`)
      } else {
        ElMessage.error(`分辨率切换失败 (${r.monitorName})`)
      }
      addLog(`分辨率切换结果: ${r.monitorName} -> ${r.result ? '成功' : '失败'}`)
    } else if (msg.type === MSG_TYPE_VIDEO_CODEC_CHANGED && msg.videoCodecChanged) {
      const c = msg.videoCodecChanged
      // VideoType: kNetH264=0, kNetHevc=1
      if (c.videoType === 1) {
        const reason = c.fullColor ? '全彩模式' : '编码设置'
        addLog(`远端编码已切换为 H.265/HEVC (${reason})`)
        ElMessageBox.alert(
          `远端已切换为 H.265/HEVC 编码（${reason}）。\n\n当前浏览器 WebRTC 仅支持 H.264，画面可能无法显示。\n请关闭全彩模式，或改用 Windows 客户端（支持 H.265）。`,
          '编码格式不兼容',
          { confirmButtonText: '知道了', type: 'warning' },
        ).catch(() => {})
      } else {
        addLog(`远端编码已切换为 H.264`)
      }
    }
    // 其余二进制控制消息(统计等)暂不处理
  }
}

// 「发送到远端」:读本地剪贴板 -> kClipboardInfo -> gr_user_proxy 写入远端系统剪贴板
async function sendClipboardToRemote(): Promise<boolean> {
  let text = ''
  try {
    text = await navigator.clipboard.readText()
  } catch (err) {
    addLog(`读取本地剪贴板失败(需页面授权/聚焦): ${String(err)}`)
    return false
  }
  if (!text) {
    addLog('本地剪贴板为空或无文本')
    return false
  }
  if (sendClipboardText(dc, form.deviceId, form.streamId, text)) {
    addLog(`已发送本地剪贴板到远端 (${text.length} 字符)`)
    return true
  }
  addLog('数据通道未连接,剪贴板发送失败')
  return false
}

// 「复制到本地」:把最近收到的远端剪贴板文本写入本地系统剪贴板
async function copyRemoteToLocal(): Promise<boolean> {
  if (!remoteClipboard.value) return false
  try {
    await navigator.clipboard.writeText(remoteClipboard.value)
    addLog('已复制远端剪贴板内容到本地')
    return true
  } catch (err) {
    addLog(`写入本地剪贴板失败: ${String(err)}`)
    return false
  }
}

// 无头/CDP 调试用:window.__clipboard / window.__perf
function exposeClipboardPerfDebug() {
  const w = window as unknown as { __clipboard?: unknown; __perf?: unknown; __mic?: unknown }
  w.__clipboard = {
    sendText: (text: string) => sendClipboardText(dc, form.deviceId, form.streamId, text),
    lastRemote: () => remoteClipboard.value,
  }
  w.__perf = () => ({ ...perf.value })
  w.__mic = {
    toggle: () => toggleMic(),
    on: () => micOn.value,
  }
}

// ---------- 文件传输(ft_data_channel)----------
// 状态与操作在 useFileTransfer composable;UI 在 FileTransferWindow.vue
let ftDc: RTCDataChannel | null = null
const ftVisible = ref(false)
const ft = useFileTransfer()
const ftReady = ft.ftReady

// 无头/CDP 调试用:window.__ft
function exposeFtDebug() {
  const w = window as unknown as { __ft?: unknown }
  w.__ft = {
    ready: () => ft.ftReady.value,
    listDir: (path: string) => ft.client()?.listDir(path),
    uploadText: async (name: string, targetDir: string, content: string) => {
      const client = ft.client()
      if (!client) throw new Error('ft not ready')
      const bytes = new TextEncoder().encode(content)
      const file = new File([bytes.slice().buffer], name)
      const result = await client.upload(file, targetDir)
      return { ...result, sha256: await sha256Hex(bytes) }
    },
    uploadFile: (file: File, targetDir: string) => {
      const client = ft.client()
      if (!client) throw new Error('ft not ready')
      return client.upload(file, targetDir)
    },
    download: async (path: string) => {
      const r = await ft.client()?.download(path)
      if (!r) throw new Error('ft not ready')
      return { taskId: r.taskId, name: r.name, size: r.size, sha256: r.sha256 }
    },
    tasks: () => ft.client()?.getTasks() ?? [],
  }
}

// 压低 Chrome 视频抖动缓冲/播放余量(跟手性关键)。
// playoutDelayHint=0:关掉自适应播放延迟;jitterBufferTarget=0:把目标缓冲钉到 0ms。
// 不支持的浏览器属性不存在,静默忽略。轨 unmute 后再设一次(部分版本 ontrack 时尚未生效)。
type LowLatencyReceiver = RTCRtpReceiver & {
  playoutDelayHint?: number
  jitterBufferTarget?: number
}
function applyLowLatencyPlayout(receiver: RTCRtpReceiver, track?: MediaStreamTrack) {
  const apply = () => {
    try {
      const r = receiver as LowLatencyReceiver
      r.playoutDelayHint = 0
      r.jitterBufferTarget = 0
    } catch {
      /* ignore */
    }
  }
  apply()
  if (track && track.readyState !== 'live') {
    track.addEventListener('unmute', apply, { once: true })
  }
}

// 连接成功(datachannel 打开)后初始化输入回传:
// 先从 render 拉取当前采集显示器名,event_replayer 按它定位回放坐标系。
// 显示器名来自编码帧回调,首帧编码完成前接口返回空,故轮询等待
async function fetchMonitorName(): Promise<string> {
  for (let i = 0; i < 15; i++) {
    try {
      const resp = await fetch('/get/render/configuration')
      const result = (await resp.json()) as { code?: number; data?: { monitor_name?: string } }
      const name = result.data?.monitor_name ?? ''
      if (name) return name
    } catch (err) {
      addLog(`获取 render 配置失败: ${String(err)}`)
    }
    await new Promise((r) => setTimeout(r, 1000))
  }
  return ''
}

async function initInput() {
  if (!inputDc || !videoRef.value) return
  const monitorName = await fetchMonitorName()
  if (!inputDc || !videoRef.value) return // 等待期间已断开
  if (!monitorName) {
    addLog('警告: 未获取到采集显示器名,鼠标事件可能被 render 丢弃')
  }
  input?.detach()
  input = new InputController({
    dc: inputDc,
    deviceId: form.deviceId,
    streamId: form.streamId,
    monitorName,
    video: videoRef.value,
    onLog: addLog,
  })
  input.viewOnly = viewOnly.value
  input.setRelativeMode(pointerLocked.value)
  input.attach()
  addLog(`输入回传已启用, monitor: ${monitorName || '(未知)'}`)
}

watch(viewOnly, (v) => {
  if (input) input.viewOnly = v
})

watch(pointerLocked, (v) => {
  input?.setRelativeMode(v)
})

function addLog(msg: string) {
  const line = `[${new Date().toLocaleTimeString()}] ${msg}`
  console.log(line)
  logs.value.push(line)
  if (logs.value.length > 200) logs.value.shift()
}

// 从 URL query 带入参数:?deviceId=&password=(明文)或 &pwd_md5=(预哈希)
// deviceId + 密码(任一形式)齐全时自动连接;流 ID 由设备 ID 派生,不从 URL 带入
const pwdMd5Override = ref('')

function loadQueryParams() {
  const q = new URLSearchParams(window.location.search)
  form.deviceId = q.get('deviceId') ?? ''
  form.password = q.get('password') ?? ''
  pwdMd5Override.value = q.get('pwd_md5') ?? ''
  // URL 未带设备 ID 时用上次成功连接的设备 ID 预填(不存密码)
  if (!form.deviceId) {
    try {
      const last = JSON.parse(localStorage.getItem(LS_LAST_CONN) ?? '{}') as {
        deviceId?: string
      }
      if (last.deviceId) form.deviceId = last.deviceId
    } catch {
      /* localStorage 不可用或数据损坏时忽略 */
    }
  }
}

function effectivePwdMd5(): string {
  if (pwdMd5Override.value) return pwdMd5Override.value
  return form.password ? CryptoJS.MD5(form.password).toString() : ''
}

function waitIceGatheringComplete(peer: RTCPeerConnection): Promise<void> {
  if (peer.iceGatheringState === 'complete') return Promise.resolve()
  return new Promise((resolve, reject) => {
    const timer = window.setTimeout(() => {
      peer.removeEventListener('icegatheringstatechange', onChange)
      reject(new Error('ICE candidate 收集超时'))
    }, ICE_GATHER_TIMEOUT_MS)
    const onChange = () => {
      addLog(`iceGatheringState: ${peer.iceGatheringState}`)
      if (peer.iceGatheringState === 'complete') {
        window.clearTimeout(timer)
        peer.removeEventListener('icegatheringstatechange', onChange)
        resolve()
      }
    }
    peer.addEventListener('icegatheringstatechange', onChange)
  })
}

function cleanup() {
  stopConnWatchdog()
  input?.detach()
  input = null
  gamepad?.disable()
  gamepadOn.value = false
  if (document.pointerLockElement) document.exitPointerLock()
  perfCollector.stop()
  perf.value = { ...EMPTY_PERF }
  remoteClipboard.value = ''
  remoteMonitors.value = []
  capturingMonitor.value = ''
  ft.resetFt('连接已断开')
  micStream?.getTracks().forEach((t) => t.stop())
  micStream = null
  micOn.value = false
  micTransceiver = null
  // __ft 调试钩子常驻(onMounted 时挂出,内部经 ft.client() 惰性取值),不随连接清理
  const w = window as unknown as { __pc?: RTCPeerConnection | null }
  w.__pc = null
  if (ftDc) {
    ftDc.onopen = null
    ftDc.onmessage = null
    ftDc.onclose = null
    ftDc.onerror = null
    try { ftDc.close() } catch { /* ignore */ }
    ftDc = null
  }
  if (pingTimer !== null) {
    window.clearInterval(pingTimer)
    pingTimer = null
  }
  pingRttMs.value = -1
  if (pingDc) {
    pingDc.onopen = null
    pingDc.onmessage = null
    pingDc.onclose = null
    pingDc.onerror = null
    try { pingDc.close() } catch { /* ignore */ }
    pingDc = null
  }
  if (inputDc) {
    inputDc.onopen = null
    inputDc.onclose = null
    inputDc.onerror = null
    try { inputDc.close() } catch { /* ignore */ }
    inputDc = null
  }
  if (dc) {
    dc.onopen = null
    dc.onmessage = null
    dc.onclose = null
    dc.onerror = null
    try { dc.close() } catch { /* ignore */ }
    dc = null
  }
  if (pc) {
    pc.ontrack = null
    pc.onconnectionstatechange = null
    pc.onicegatheringstatechange = null
    pc.onicecandidate = null
    try { pc.close() } catch { /* ignore */ }
    pc = null
  }
  hasVideo.value = false
  if (videoRef.value) videoRef.value.srcObject = null
}

async function connect() {
  if (!form.deviceId) {
    errorMsg.value = '请填写设备 ID'
    status.value = 'failed'
    return
  }
  cleanup()
  status.value = 'connecting'
  errorMsg.value = ''

  try {
    // 不配置任何 iceServers:render 端会把 candidate 改写为客户端可达地址
    pc = new RTCPeerConnection()
    // 无头/CDP 调试用:getStats 等诊断入口
    ;(window as unknown as { __pc?: RTCPeerConnection | null }).__pc = pc

    // 麦克风上行占位:建连即创建 sendrecv 音频 transceiver(此时无本地轨),
    // 之后开/关 mic 仅 replaceTrack,避免重新协商(render 只处理一次 offer)
    micTransceiver = pc.addTransceiver('audio', { direction: 'sendrecv' })

    pc.ontrack = (ev: RTCTrackEvent) => {
      applyLowLatencyPlayout(ev.receiver, ev.track)
      const el = videoRef.value
      if (!el) {
        addLog(`ontrack: kind=${ev.track.kind} (no video element yet)`)
        return
      }
      // render 端 video/audio 可能落在不同 MediaStream id 上;直接 srcObject=streams[0]
      // 会互相覆盖(后到的轨把先到的踢掉)——典型症状:有画面但始终无声。
      // 统一汇入同一个 MediaStream,保证 video+audio 同挂在 <video> 上。
      let ms = el.srcObject as MediaStream | null
      if (!ms) {
        ms = new MediaStream()
        el.srcObject = ms
      }
      if (!ms.getTrackById(ev.track.id)) {
        ms.addTrack(ev.track)
      }
      if (ev.track.kind === 'video') {
        hasVideo.value = true
      }
      const kinds = ms.getTracks().map((t) => `${t.kind}:${t.readyState}`).join(',')
      addLog(
        `ontrack: kind=${ev.track.kind} muted=${ev.track.muted} enabled=${ev.track.enabled} ` +
          `streamIds=${ev.streams.map((s) => s.id).join('|') || '-'} el=[${kinds}] pageMuted=${muted.value}`,
      )
      // 有声轨时提醒:默认静音是为了过 autoplay 策略,需点悬浮球取消静音
      if (ev.track.kind === 'audio' && muted.value) {
        addLog('已收到远端音频轨;页面默认静音,请点悬浮球扬声器图标取消静音')
      }
    }

    pc.onconnectionstatechange = () => {
      const state = pc?.connectionState
      addLog(`connectionState: ${state}`)
      if (state === 'connected') {
        // 连接(或重连)成功:取消挂起的重连、清零重试计数
        cancelReconnectTimer()
        reconnectCount.value = 0
        manualClose = false
        status.value = 'connected'
        // 记忆本次连接参数,下次打开页面预填(不存密码)
        try {
          localStorage.setItem(
            LS_LAST_CONN,
            JSON.stringify({ deviceId: form.deviceId, streamId: form.streamId }),
          )
        } catch {
          /* ignore */
        }
        if (pc) {
          // 连接就绪后对所有 receiver 再压一次播放延迟(防 ontrack 时机偏早未生效)
          for (const receiver of pc.getReceivers()) {
            if (receiver.track?.kind === 'video') {
              applyLowLatencyPlayout(receiver, receiver.track)
            }
          }
          perfCollector.start(pc)
        }
        startConnWatchdog()
      } else if (state === 'failed' || state === 'disconnected' || state === 'closed') {
        // 非手动断开(manualClose 时 cleanup 已摘掉本回调,closed 不会走到这里)自动重连
        scheduleReconnect(state)
      }
    }

    // 数据通道,为后续控制消息预留
    dc = pc.createDataChannel(DATA_CHANNEL_LABEL)
    dc.binaryType = 'arraybuffer' // render 会推 kClipboardInfo 等二进制控制消息
    dc.onopen = () => {
      addLog(`datachannel "${DATA_CHANNEL_LABEL}" onopen`)
      // 发 kHello 触发 render 回推 kServerConfiguration(显示器列表/可用分辨率/采集显示器名)
      sendControlMessage(dc, form.deviceId, form.streamId, {
        type: MSG_TYPE_HELLO,
        hello: {
          enableAudio: true,
          enableVideo: true,
          enableController: true,
          clientType: 100, // ClientType.kUnknown
          deviceName: 'web_client',
        },
      })
      // ?force420=1:请求 render 关闭全彩(yuv444)模式。Chrome 无法硬解
      // H264 High4:4:4,只能软解——1080p60 软解会丢帧+处理延迟飙升,
      // 症状正是 fps 低+不跟手。与原生客户端同款消息(kSwitchFullColorMode),
      // 注意该消息会改动 render 端保存的设置(原生端下次连接会按其自身设置再切换)
      if (new URLSearchParams(window.location.search).get('force420') === '1') {
        sendControlMessage(dc, form.deviceId, form.streamId, {
          type: MSG_TYPE_SWITCH_FULL_COLOR_MODE,
          switchFullColorMode: { enable: false },
        })
        addLog('已请求 render 关闭全彩模式(kSwitchFullColorMode: false, force420=1)')
      }
      // 输入初始化挪到 input_data_channel 的 onopen(输入走专用不可靠通道)
    }
    dc.onmessage = (ev: MessageEvent) => {
      if (ev.data instanceof ArrayBuffer) {
        handleDcBinary(ev.data)
      } else {
        addLog(`datachannel onmessage: ${String(ev.data).slice(0, 200)}`)
      }
    }
    dc.onclose = () => addLog('datachannel onclose')
    dc.onerror = (ev: Event) => addLog(`datachannel onerror: ${String(ev)}`)

    // 文件传输通道(render 侧 rtc_server.cpp:90 按此名字识别)
    ftDc = pc.createDataChannel(FT_DATA_CHANNEL_LABEL)
    ftDc.binaryType = 'arraybuffer'
    ftDc.onopen = () => {
      addLog(`datachannel "${FT_DATA_CHANNEL_LABEL}" onopen`)
      ft.initFt(ftDc as RTCDataChannel, form.deviceId, form.streamId, addLog)
    }
    ftDc.onmessage = (ev: MessageEvent) => {
      if (ev.data instanceof ArrayBuffer) ft.handleChannelMessage(ev.data)
    }
    ftDc.onclose = () => {
      addLog('ft datachannel onclose')
      ft.resetFt('文件传输通道已断开')
    }
    ftDc.onerror = (ev: Event) => addLog(`ft datachannel onerror: ${String(ev)}`)

    // 输入专用通道:不可靠(不重传)+不保序。鼠标移动/滚轮是高频绝对坐标事件,
    // 走默认 ordered+reliable 通道时一旦丢包会产生队头阻塞(后续输入全部排队
    // 等重传),表现为操作不跟手;丢一帧绝对坐标无副作用,下帧即覆盖,
    // 与原生客户端 UDP 输入语义对齐。控制/剪贴板消息仍走可靠 media 通道。
    inputDc = pc.createDataChannel(INPUT_DATA_CHANNEL_LABEL, {
      ordered: false,
      maxRetransmits: 0,
    })
    inputDc.binaryType = 'arraybuffer'
    inputDc.onopen = () => {
      addLog(`datachannel "${INPUT_DATA_CHANNEL_LABEL}" onopen (unreliable/unordered)`)
      void (async () => {
        // fetchMonitorName 依赖 media 通道回推配置,先等它就绪
        for (let i = 0; i < 50 && dc?.readyState !== 'open'; i++) {
          await new Promise((r) => setTimeout(r, 100))
        }
        void initInput()
      })()
    }
    inputDc.onclose = () => addLog('input datachannel onclose')
    inputDc.onerror = (ev: Event) => addLog(`input datachannel onerror: ${String(ev)}`)

    // 诊断 ping 通道:render 收到即原样回显。500ms 发一个 8 字节时间戳,
    // 往返差即 datachannel RTT(输入通道延迟的直接度量,排除 ICE RTT 的混淆)
    pingDc = pc.createDataChannel(PING_DATA_CHANNEL_LABEL, {
      ordered: false,
      maxRetransmits: 0,
    })
    pingDc.binaryType = 'arraybuffer'
    pingDc.onopen = () => {
      addLog(`datachannel "${PING_DATA_CHANNEL_LABEL}" onopen (echo)`)
      pingTimer = window.setInterval(() => {
        if (pingDc?.readyState !== 'open') return
        const buf = new ArrayBuffer(8)
        new DataView(buf).setFloat64(0, performance.now())
        pingDc.send(buf)
      }, 500)
    }
    pingDc.onmessage = (ev: MessageEvent) => {
      if (ev.data instanceof ArrayBuffer && ev.data.byteLength === 8) {
        pingRttMs.value = performance.now() - new DataView(ev.data).getFloat64(0)
      }
    }
    pingDc.onclose = () => addLog('ping datachannel onclose')
    pingDc.onerror = (ev: Event) => addLog(`ping datachannel onerror: ${String(ev)}`)

    const offer = await pc.createOffer({
      offerToReceiveAudio: true,
      offerToReceiveVideo: true,
    })
    await pc.setLocalDescription(offer)
    // 诊断:SDP 里的帧率上限会压死 webrtc 输入侧推帧(VideoSinkWants.max_framerate)
    const scanFr = (tag: string, sdp: string) => {
      const lines = sdp.split('\n').filter((l) => /framerate|max-fr/i.test(l))
      addLog(lines.length ? `${tag} 帧率相关: ${lines.map((l) => l.trim()).join(' | ')}` : `${tag} 无帧率上限行`)
    }
    scanFr('offer', offer.sdp ?? '')

    // 等 ICE gathering complete 再发 offer,不做 trickle
    addLog('等待 ICE gathering complete ...')
    await waitIceGatheringComplete(pc)
    addLog('ICE gathering 完成,发送信令')

    const localDesc = pc.localDescription
    if (!localDesc?.sdp) throw new Error('本地 SDP 为空')

    // 发信令拿 answer;返回空字符串表示"连接被占用"(code 704),由调用方决定接管或放弃
    const postSignal = async (takeover: boolean): Promise<string> => {
      const query = new URLSearchParams({
        device_id: form.deviceId,
        stream_id: form.streamId,
        safety_pwd_md5: effectivePwdMd5(),
      })
      if (takeover) query.set('takeover', '1')
      const resp = await fetch(`${SIGNAL_URL}?${query.toString()}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sdp: localDesc.sdp }),
      })
      if (!resp.ok) throw new Error(`信令请求失败: HTTP ${resp.status}`)
      const result = (await resp.json()) as {
        code?: number
        message?: string
        data?: { answer_sdp?: string }
      }
      if (result.code === SIGNAL_CODE_OCCUPIED) return ''
      if (result.code !== 200) {
        throw new Error(`信令被拒: ${result.message ?? `code=${result.code}`}`)
      }
      const answer = result.data?.answer_sdp
      if (!answer) throw new Error('信令响应缺少 answer_sdp')
      return answer
    }

    let answerSdp = await postSignal(false)
    if (!answerSdp) {
      // 该设备已有活跃连接:询问是否接管,同意则顶掉对方
      if (!window.confirm('该设备当前已有连接在线,是否接管?(对方的连接将被断开)')) {
        throw new Error('设备已被连接,未接管')
      }
      addLog('连接被占用,用户确认接管,带 takeover 重新发起信令')
      answerSdp = await postSignal(true)
    }

    scanFr('answer', answerSdp)
    await pc.setRemoteDescription({ type: 'answer', sdp: answerSdp })
    addLog('已设置远端 answer,等待连接建立')
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err)
    addLog(`连接失败: ${msg}`)
    cleanup()
    if (!manualClose && reconnectCount.value > 0) {
      // 自动重连过程中的失败:计入重试,继续排队下一次
      scheduleReconnect(`重连失败(${msg})`)
    } else {
      status.value = 'failed'
      errorMsg.value = msg
    }
  }
}

// 手动点「连接/重新连接」:清零重试计数
function manualConnect() {
  manualClose = false
  cancelReconnectTimer()
  reconnectCount.value = 0
  void connect()
}

function disconnect() {
  manualClose = true
  cancelReconnectTimer()
  reconnectCount.value = 0
  cleanup()
  status.value = 'idle'
  errorMsg.value = ''
  addLog('已断开')
}

const statusText: Record<ConnStatus, string> = {
  idle: '未连接',
  connecting: '连接中',
  connected: '已连接',
  failed: '失败',
  reconnecting: '重连中',
}
const statusType: Record<ConnStatus, 'info' | 'warning' | 'success' | 'danger'> = {
  idle: 'info',
  connecting: 'warning',
  connected: 'success',
  failed: 'danger',
  reconnecting: 'warning',
}
// 重连中带上第几次
const statusLabel = computed(() =>
  status.value === 'reconnecting' ? `重连中(第 ${reconnectCount.value} 次)` : statusText[status.value],
)

// 无头/CDP 调试用:window.__input / window.__conn / window.__gamepad
function exposeInputConnDebug() {
  const w = window as unknown as { __input?: unknown; __conn?: unknown; __gamepad?: unknown }
  w.__input = {
    lastMouse: () => input?.lastMouse ?? null,
    relative: () => input?.relativeMode ?? false,
    virtualPos: () => input?.virtualPos() ?? null,
  }
  w.__conn = {
    status: () => status.value,
    reconnectCount: () => reconnectCount.value,
    pointerLocked: () => pointerLocked.value,
  }
  // 手柄调试:enable/testSend 可在无头 Chrome(无物理手柄)下验证打包与 render 回放链路
  w.__gamepad = {
    toggle: () => toggleGamepad(),
    on: () => gamepadOn.value,
    status: () => gamepadStatus.value,
    poll: () => gamepad?.poll(),
    testSend: (s: GamepadSnapshot) => getGamepad().sendState(s, true),
  }
}

// ---------- 被控端 render 版本(确认对端是否为旧版;旧版接口无 app_version 字段)----------
const renderVersion = ref('')

async function fetchRenderVersion() {
  try {
    const resp = await fetch('/get/render/configuration')
    const result = (await resp.json()) as { code?: number; data?: { app_version?: string } }
    renderVersion.value = result.data?.app_version || ''
  } catch {
    /* 接口不可达时保持空 */
  }
}

onMounted(() => {
  loadQueryParams()
  exposeClipboardPerfDebug()
  exposeInputConnDebug()
  exposeFtDebug()
  void fetchRenderVersion()
  document.addEventListener('pointerlockchange', onPointerLockChange)
  // 参数齐全时自动连接(便于 CMS 跳转/无头测试);流 ID 已由设备 ID 派生
  if (form.deviceId && effectivePwdMd5()) {
    manualConnect()
  }
})
onBeforeUnmount(() => {
  manualClose = true
  cancelReconnectTimer()
  document.removeEventListener('pointerlockchange', onPointerLockChange)
  cleanup()
})
</script>

<template>
  <div class="page">
    <!-- 远端画面全屏显示 -->
    <video
      ref="videoRef"
      class="remote-video"
      autoplay
      playsinline
      :muted="muted"
      :class="{ hidden: !hasVideo }"
    ></video>

    <!-- 顶部控制条 -->
    <div class="toolbar">
      <el-form inline @submit.prevent>
        <el-form-item label="设备 ID">
          <el-input v-model="form.deviceId" placeholder="deviceId" style="width: 140px" />
        </el-form-item>
        <el-form-item label="流 ID">
          <el-tooltip content="由设备 ID 自动生成,同一设备同时只允许一路连接" placement="bottom">
            <el-input v-model="form.streamId" readonly placeholder="由设备 ID 生成" style="width: 140px" />
          </el-tooltip>
        </el-form-item>
        <el-form-item label="密码">
          <el-input
            v-model="form.password"
            type="password"
            show-password
            placeholder="password"
            style="width: 160px"
          />
        </el-form-item>
        <el-form-item>
          <el-button
            type="primary"
            :loading="status === 'connecting'"
            :disabled="status === 'connected' || status === 'reconnecting'"
            @click="manualConnect"
          >
            {{ status === 'failed' ? '重新连接' : '连接' }}
          </el-button>
          <el-button
            v-if="status === 'connected' || status === 'connecting' || status === 'reconnecting'"
            @click="disconnect"
          >
            断开
          </el-button>
        </el-form-item>
        <el-form-item>
          <el-tag :type="statusType[status]">{{ statusLabel }}</el-tag>
          <el-button size="small" class="log-toggle" @click="logVisible = !logVisible">
            日志
          </el-button>
          <el-tooltip
            :content="renderVersion ? `被控端 render 版本: ${renderVersion}` : '被控端为旧版本(未上报版本号)'"
            placement="bottom"
          >
            <span class="render-version">v{{ renderVersion || '旧版' }}</span>
          </el-tooltip>
        </el-form-item>
      </el-form>
      <el-alert
        v-if="status === 'failed' && errorMsg"
        :title="errorMsg"
        type="error"
        :closable="false"
        class="error-alert"
      />
    </div>

    <!-- 悬浮球:点开白色圆角菜单面板(本地功能 + 远程控制) -->
    <FloatBall
      v-model:muted="muted"
      v-model:mic-on="micOn"
      v-model:view-only="viewOnly"
      v-model:ft-visible="ftVisible"
      v-model:perf-visible="perfVisible"
      :connected="status === 'connected'"
      :ft-ready="ftReady"
      :perf="perf"
      :remote-clipboard="remoteClipboard"
      :send="sendControl"
      :send-clipboard-to-remote="sendClipboardToRemote"
      :copy-remote-to-local="copyRemoteToLocal"
      :toggle-mic="toggleMic"
      :get-video="() => videoRef"
      :pointer-locked="pointerLocked"
      :gamepad-on="gamepadOn"
      :gamepad-status="gamepadStatus"
      :toggle-gamepad="toggleGamepad"
      :monitors="remoteMonitors"
      :capturing-monitor="capturingMonitor"
      :log="addLog"
    />

    <!-- 指针锁定提示(锁定期间物理光标隐藏,顶部常驻提示) -->
    <div v-if="pointerLocked" class="lock-hint">鼠标已锁定 · 相对模式 · Esc 退出</div>

    <!-- 性能面板:每 2s 采样 pc.getStats();码率为 WebRTC 自适应值(协议无改码率消息) -->
    <div v-if="perfVisible" class="perf-panel">
      <div class="perf-item">
        <span class="perf-label">码率</span>
        <span class="perf-value">{{ perfBitrateText }}</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">帧率</span>
        <span class="perf-value">{{ perf.fps.toFixed(0) }} fps</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">解码</span>
        <span class="perf-value">{{ perf.decFps.toFixed(0) }} fps</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">丢帧</span>
        <span class="perf-value">{{ perf.dropFps.toFixed(1) }}/s</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">解码耗时</span>
        <span class="perf-value">{{ perf.decodeMs.toFixed(1) }} ms</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">处理耗时</span>
        <span class="perf-value">{{ perf.procMs.toFixed(1) }} ms</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">解码器</span>
        <span class="perf-value">{{ perf.decoder || '-' }}</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">卡顿</span>
        <span class="perf-value">{{ perf.freezes }} 次/{{ perf.freezeMs.toFixed(0) }} ms</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">缓冲目标</span>
        <span class="perf-value">{{ perf.jbTargetMs.toFixed(0) }} ms</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">缓冲实际</span>
        <span class="perf-value">{{ perf.jbDelayMs.toFixed(0) }} ms</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">RTT</span>
        <span class="perf-value">{{ perfRttText }}</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">输入RTT</span>
        <span class="perf-value">{{ pingRttMs >= 0 ? pingRttMs.toFixed(1) + ' ms' : '-' }}</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">丢包</span>
        <span class="perf-value">{{ perfLossText }}</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">抖动</span>
        <span class="perf-value">{{ perfJitterText }}</span>
      </div>
      <div class="perf-item">
        <span class="perf-label">分辨率</span>
        <span class="perf-value">{{ perfResolutionText }}</span>
      </div>
      <div class="perf-item perf-path">
        <span class="perf-label">路径</span>
        <span class="perf-value">{{ perf.localCand || '?' }} ↔ {{ perf.remoteCand || '?' }}</span>
      </div>
      <span class="perf-note">码率为 WebRTC 自适应,协议不支持手动指定</span>
    </div>

    <!-- 独立文件传输窗口(本地暂存区 / 远端文件 / 传输记录) -->
    <FileTransferWindow v-model:visible="ftVisible" :device-id="form.deviceId" :ft="ft" />

    <!-- 日志面板(默认收起,顶部「日志」按钮切换) -->
    <div v-if="logVisible && logs.length" class="log-panel">
      <div v-for="(line, i) in logs" :key="i" class="log-line">{{ line }}</div>
    </div>
  </div>
</template>

<style scoped>
.page {
  position: fixed;
  inset: 0;
  background: #000;
  overflow: hidden;
}
.remote-video {
  width: 100%;
  height: 100%;
  object-fit: contain;
  /* 触屏手势交由 input.ts 处理,禁用浏览器默认滚动/缩放 */
  touch-action: none;
  /* 避免额外滤镜/缩放合成层;独立层减少与 UI 叠层的合成延迟 */
  transform: translateZ(0);
}
.remote-video.hidden {
  visibility: hidden;
}
.toolbar {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  padding: 10px 16px 0;
  background: rgba(30, 30, 30, 0.85);
}
.toolbar :deep(.el-form-item__label) {
  color: #eee;
}
.error-alert {
  margin-bottom: 10px;
}
.lock-hint {
  position: absolute;
  top: 64px;
  left: 50%;
  transform: translateX(-50%);
  padding: 6px 14px;
  background: rgba(230, 162, 60, 0.9);
  color: #fff;
  border-radius: 6px;
  font-size: 13px;
  pointer-events: none;
  z-index: 20;
}
.log-panel {
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  max-height: 30vh;
  overflow-y: auto;
  padding: 8px 16px;
  background: rgba(0, 0, 0, 0.7);
  color: #9f9;
  font-family: monospace;
  font-size: 12px;
  pointer-events: none;
}
.log-line {
  white-space: pre-wrap;
  word-break: break-all;
}
.log-toggle {
  margin-left: 8px;
}
.render-version {
  margin-left: 8px;
  font-size: 12px;
  color: #909399;
  cursor: default;
  user-select: none;
}
.perf-panel {
  position: absolute;
  top: 64px;
  right: 16px;
  display: flex;
  align-items: flex-start;
  gap: 12px;
  padding: 8px 10px;
  background: rgba(30, 30, 30, 0.9);
  border-radius: 8px;
  z-index: 30;
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
</style>
