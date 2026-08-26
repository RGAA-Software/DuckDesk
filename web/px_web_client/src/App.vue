<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { exchangeRenewalTicket } from './ticket_renewal'
import type { RtcSessionIceConfig } from './ticket_renewal'
import CryptoJS from 'crypto-js'
import { InputController } from './rtc/input'
import { sendControlMessage } from './rtc/control'
import { GamepadController } from './rtc/gamepad'
import type { GamepadSnapshot } from './rtc/gamepad'
import { PerfCollector, EMPTY_PERF, perfSummaryLine } from './rtc/stats'
import type { PerfStats } from './rtc/stats'
import {
  sendClipboardText,
  parseClipboardText,
  parseClipboardResponseText,
  canReadLocalClipboard,
} from './rtc/clipboard'
import { decodeConnectToken } from './rtc/connect_token'
import { decodeMessage } from './rtc/proto'
import { StandardRtcSignaling } from './rtc/standard_signaling'
import { applyDocumentTitle } from './locales/i18n'
import logoUrl from './assets/px_icon.png'
import {
  MSG_TYPE_HELLO,
  MSG_TYPE_CLIPBOARD_INFO,
  MSG_TYPE_CLIPBOARD_INFO_RESP,
  MSG_TYPE_SERVER_CONFIGURATION,
  MSG_TYPE_MONITOR_SWITCHED,
  MSG_TYPE_CHANGE_MONITOR_RESOLUTION_RESULT,
  MSG_TYPE_SWITCH_FULL_COLOR_MODE,
  MSG_TYPE_CONNECTION_TAKEN_OVER,
  MSG_TYPE_INSTANCE_STOPPED,
  MSG_TYPE_VIDEO_CODEC_CHANGED,
  MSG_TYPE_GAME_STATUS_CHANGED,
  MSG_TYPE_VIRTUAL_DISPLAY_REQUEST,
  MSG_TYPE_VIRTUAL_DISPLAY_RESPONSE,
  MSG_TYPE_VOICE_CALL_REQUEST,
  MSG_TYPE_VOICE_CALL_RESPONSE,
  MSG_TYPE_VOICE_AUDIO_CONFIG,
} from './rtc/proto'
import { TlvReassembler } from './rtc/tlv'
import {
  isSupportedVoiceAudioConfig,
  matchesActiveVoiceCall,
  matchesPendingVoiceResponse,
} from './rtc/voice_call_state'
import type { VoiceCallPhase } from './rtc/voice_call_state'
import { ElMessage, ElMessageBox, ElNotification } from 'element-plus'
import FloatBall from './FloatBall.vue'
import FileTransferWindow from './FileTransferWindow.vue'
import { useFileTransfer } from './useFileTransfer'
import { sha256Hex } from './rtc/file_transfer'

const { t } = useI18n()

const MAX_LOG_LINES = 8000
const clipboardAvailable = canReadLocalClipboard()

// ---------- 信令契约(对齐 render net_ws http_handler.cpp)----------
// POST /alloc/local/rtc?device_id=X&stream_id=Y&safety_pwd_md5=md5(安全密码或临时密码)[&takeover=1]
// 请求体: { "sdp": "<offer>", "ticket"?, "client_nonce"?, "instance_id"? }
// 响应: { "code":200, "message":"ok", "data": { "answer_sdp": "..." } }
// code=704(kHandlerErrRtcLocalOccupied): 同 stream_id 已有活跃连接,需用户确认后带 takeover=1 重试
const SIGNAL_URL = '/alloc/local/rtc'
const SIGNAL_CODE_OCCUPIED = 704
const DATA_CHANNEL_LABEL = 'media_data_channel' // render 端按此名字识别(rtc_server.cpp:81)
const FT_DATA_CHANNEL_LABEL = 'ft_data_channel' // 文件传输通道(rtc_server.cpp:90)
const INPUT_DATA_CHANNEL_LABEL = 'input_data_channel' // 输入专用不可靠通道(render 端按此名字识别)
const PING_DATA_CHANNEL_LABEL = 'ping_data_channel' // 诊断通道:render 收到即回显,实测 datachannel RTT
const ICE_GATHER_TIMEOUT_MS = 10000
const STANDARD_RTC_CONFIG_POLL_MS = 60000

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
const LS_LAST_CONN = 'px_web_client.last_conn'
// 浏览器标识:Console launch 页经 URL 带入(&nonce=),裸开页面时本地生成持久化。
// 信令带给 render,同一浏览器(nonce 相同)的新连接自动接管旧连接,不弹确认
const LS_CLIENT_NONCE = 'px_web_client.client_nonce'
const clientNonce = ref('')
// Console launch parameters arrive in the URL fragment so the one-time ticket is
// never sent in an HTTP Referer or server access log. Keep them in memory only.
const connectionTicket = ref('')
const connectionInstanceId = ref('')
const renewalToken = ref('')
const renewalUrl = ref('')
const grantedPermissions = ref<string[]>([])
const requestedConnectionType = ref<'rtc_direct' | 'rtc'>('rtc_direct')
const relayHost = ref('')
const relayPort = ref(0)
let rtcIceConfig: RtcSessionIceConfig | null = null
let forceStandardRtc = false
let ticketConsumed = false

function hasGrantedPermission(permission: string) {
  // Manual administrator/debug connections do not carry a Console capability
  // list and retain the legacy controls.
  return grantedPermissions.value.length === 0 || grantedPermissions.value.includes(permission)
}

async function renewConnectionTicket(updateProgress = true) {
  if (updateProgress) setConnectStep('signal', '正在向 Console 申请新的重连票据')
  const renewed = await exchangeRenewalTicket(
    fetch,
    renewalUrl.value,
    renewalToken.value,
    clientNonce.value,
  )
  connectionTicket.value = renewed.ticket
  renewalToken.value = renewed.renewalToken
  if (renewed.permissions.length > 0) grantedPermissions.value = renewed.permissions
  if (renewed.rtcIceConfig) rtcIceConfig = renewed.rtcIceConfig
  ticketConsumed = false
  addLog('[connect] 已轮换 Console 重连票据')
  return renewed
}

function ensureClientNonce(urlNonce: string) {
  if (urlNonce) {
    clientNonce.value = urlNonce
    return
  }
  try {
    let n = localStorage.getItem(LS_CLIENT_NONCE) ?? ''
    if (!n) {
      n =
        typeof crypto.randomUUID === 'function'
          ? crypto.randomUUID()
          : `${Date.now()}${Math.random().toString(16).slice(2)}`
      localStorage.setItem(LS_CLIENT_NONCE, n)
    }
    clientNonce.value = n
  } catch {
    /* localStorage 不可用时每次随机,退化为不带标识(占用仍弹确认) */
    clientNonce.value = `${Date.now()}${Math.random().toString(16).slice(2)}`
  }
}

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
  const standardFallbackReady = relayHost.value && relayPort.value > 0
    && (!connectionTicket.value || rtcIceConfig !== null)
  if (!isStandardRtc() && standardFallbackReady) {
    forceStandardRtc = true
    addLog(connectionTicket.value
      ? `[rtc-route] Direct 会话未连通(${reason})，自动换新票据并重开 RTC Standard`
      : `[rtc-route] Direct 会话未连通(${reason})，使用设备密码重开 RTC Standard`)
  }
  if (reconnectCount.value >= MAX_AUTO_RECONNECT) {
    status.value = 'failed'
    errorMsg.value = `连接${reason},自动重连 ${MAX_AUTO_RECONNECT} 次后仍失败`
    setConnectStep(connectStep.value === 'idle' ? 'init' : connectStep.value, errorMsg.value)
    addLog(errorMsg.value)
    return
  }
  reconnectCount.value += 1
  status.value = 'reconnecting'
  errorMsg.value = ''
  setConnectStep(
    'reconnect',
    `${reason}, ${RECONNECT_DELAY_MS / 1000}s 后第 ${reconnectCount.value}/${MAX_AUTO_RECONNECT} 次`,
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
  applyDocumentTitle(id)
}, { immediate: true, flush: 'sync' })

const status = ref<ConnStatus>('idle')
const errorMsg = ref('')
const logs = ref<string[]>([])
const videoRef = ref<HTMLVideoElement | null>(null)
const voiceAudioRef = ref<HTMLAudioElement | null>(null)
const hasVideo = ref(false)
// 仅观看:勾选后不回传鼠标键盘输入(开关在悬浮工具条内)
const viewOnly = ref(false)
// 声音:默认静音以保证自动播放,工具条内可开关
const muted = ref(true)
const voiceBrowserMediaAvailable = window.isSecureContext
  && typeof navigator.mediaDevices?.getUserMedia === 'function'
const voiceCallSupported = ref(false)
const voiceCallRequiresHeadset = ref(false)
const voiceCallPhase = ref<VoiceCallPhase>('idle')
const voiceCallReason = ref('')
const voiceMicMuted = ref(false)
const voiceSpeakerMuted = ref(false)
const micOn = ref(false)
let micTransceiver: RTCRtpTransceiver | null = null
let micStream: MediaStream | null = null
let voiceCallId = ''
let voiceRequestId = ''
let voiceTimeout: number | null = null
let lastMicTrackStopState = 'none'
let voiceStartGeneration = 0
let voicePreflightShown = false
let voicePreflightPending = false

function currentVoiceIdentity() {
  return { callId: voiceCallId, requestId: voiceRequestId }
}

function clearVoiceTimeout() {
  if (voiceTimeout !== null) window.clearTimeout(voiceTimeout)
  voiceTimeout = null
}

async function stopVoiceMedia() {
  if (micTransceiver) {
    try { await micTransceiver.sender.replaceTrack(null) } catch { /* ignore */ }
  }
  const tracks = micStream?.getTracks() ?? []
  tracks.forEach((track) => track.stop())
  if (tracks.length > 0) {
    lastMicTrackStopState = tracks.map((track) => track.readyState).join(',')
  }
  micStream = null
  micOn.value = false
  voiceMicMuted.value = false
}

async function finishVoiceCall(reason = '', phase: VoiceCallPhase = 'idle') {
  ++voiceStartGeneration
  clearVoiceTimeout()
  await stopVoiceMedia()
  voicePreflightShown = false
  voiceSpeakerMuted.value = false
  if (voiceAudioRef.value) voiceAudioRef.value.muted = false
  voiceCallId = ''
  voiceRequestId = ''
  voiceCallReason.value = reason
  voiceCallPhase.value = phase
}

async function hangUpVoiceCall(notifyRemote = true) {
  if (notifyRemote && voiceCallId && voiceRequestId) {
    sendControl({
      type: MSG_TYPE_VOICE_CALL_REQUEST,
      voiceCallRequest: { callId: voiceCallId, requestId: voiceRequestId, connect: false },
    })
  }
  await finishVoiceCall()
  addLog('语音通话已结束')
}

async function startVoiceCall() {
  if (!voiceCallSupported.value) {
    ElMessage.warning(voiceCallReason.value || '被控端不支持语音通话')
    return
  }
  if (!hasGrantedPermission('audio')) {
    addLog('当前连接票据未授予音频权限')
    ElMessage.warning('当前连接未授予音频权限')
    return
  }
  if (!pc || !micTransceiver) {
    addLog('尚未连接,无法发起语音通话')
    return
  }
  if (voiceCallPhase.value === 'outgoing' || voiceCallPhase.value === 'connected') return
  if (voiceCallRequiresHeadset.value && !voicePreflightShown) {
    if (voicePreflightPending) return
    voicePreflightPending = true
    try {
      await ElMessageBox.confirm(
        t('float.voicePreflightWarning'),
        t('float.voicePreflightTitle'),
        {
          confirmButtonText: t('float.voicePreflightContinue'),
          cancelButtonText: t('common.cancel'),
          type: 'warning',
        },
      )
      voicePreflightShown = true
    } catch {
      return
    } finally {
      voicePreflightPending = false
    }
  }
  const generation = ++voiceStartGeneration
  voiceCallPhase.value = 'outgoing'
  voiceCallReason.value = ''
  let acquiredStream: MediaStream | null = null
  try {
    // The click is a browser user gesture. Unlock the dedicated call playout
    // element now so a later accepted track is not silently autoplay-blocked.
    try { await voiceAudioRef.value?.play() } catch { /* track may not be attached yet */ }
    acquiredStream = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true },
    })
    if (generation !== voiceStartGeneration || voiceCallPhase.value !== 'outgoing') {
      acquiredStream.getTracks().forEach((mediaTrack) => mediaTrack.stop())
      return
    }
    micStream = acquiredStream
    const track = micStream.getAudioTracks()[0]
    if (!track) throw new Error('未获取到音频轨')
    lastMicTrackStopState = 'none'
    track.enabled = true
    track.onended = () => {
      if (voiceCallPhase.value !== 'idle') {
        void hangUpVoiceCall(true)
        ElMessage.error('麦克风设备已断开，语音通话已结束')
      }
    }
    voiceCallId = typeof crypto.randomUUID === 'function'
      ? crypto.randomUUID()
      : `web-${Date.now()}-${Math.random().toString(16).slice(2)}`
    voiceRequestId = String(Date.now())
    if (!sendControl({
      type: MSG_TYPE_VOICE_CALL_REQUEST,
      voiceCallRequest: { callId: voiceCallId, requestId: voiceRequestId, connect: true },
    })) {
      throw new Error('控制通道未连接')
    }
    addLog('语音呼叫等待被控端授权')
    voiceTimeout = window.setTimeout(() => {
      if (voiceCallPhase.value === 'outgoing') {
        void hangUpVoiceCall(true).then(() => {
          voiceCallReason.value = 'timeout'
          voiceCallPhase.value = 'error'
          ElMessage.warning('语音呼叫等待授权超时')
        })
      }
    }, 30_500)
  } catch (err) {
    acquiredStream?.getTracks().forEach((track) => track.stop())
    if (generation !== voiceStartGeneration) return
    addLog(`发起语音通话失败: ${String(err)}`)
    await finishVoiceCall(String(err), 'error')
    ElMessage.error('无法使用麦克风，请检查浏览器权限和输入设备')
  }
}

async function toggleVoiceCall() {
  if (voiceCallPhase.value === 'outgoing' || voiceCallPhase.value === 'connected') {
    await hangUpVoiceCall(true)
  } else {
    await startVoiceCall()
  }
}

function toggleVoiceMute() {
  if (voiceCallPhase.value !== 'connected' || !micStream) return
  voiceMicMuted.value = !voiceMicMuted.value
  micStream.getAudioTracks().forEach((track) => { track.enabled = !voiceMicMuted.value })
  micOn.value = !voiceMicMuted.value
  addLog(voiceMicMuted.value ? '通话麦克风已静音' : '通话麦克风已恢复')
}

function toggleVoiceSpeakerMute() {
  if (voiceCallPhase.value !== 'connected') return
  voiceSpeakerMuted.value = !voiceSpeakerMuted.value
  if (voiceAudioRef.value) voiceAudioRef.value.muted = voiceSpeakerMuted.value
  addLog(voiceSpeakerMuted.value ? '通话扬声器已静音' : '通话扬声器已恢复')
}

async function handleVoiceCallResponse(response: {
  callId: string
  requestId: number | { toString(): string }
  accepted: boolean
  reason: string
}) {
  if (!matchesPendingVoiceResponse(
    voiceCallPhase.value, currentVoiceIdentity(), response,
  )) {
    addLog('已丢弃不匹配或过期的语音响应')
    return
  }
  clearVoiceTimeout()
  if (!response.accepted) {
    const reason = response.reason || 'rejected'
    await finishVoiceCall(reason, 'error')
    ElMessage.warning(`语音呼叫未接通：${reason}`)
    return
  }
  const track = micStream?.getAudioTracks()[0]
  if (!track || !micTransceiver) {
    await hangUpVoiceCall(true)
    voiceCallReason.value = 'no_mic'
    voiceCallPhase.value = 'error'
    ElMessage.error('麦克风轨道不可用，已取消语音通话')
    return
  }
  const generation = voiceStartGeneration
  try {
    await micTransceiver.sender.replaceTrack(track)
    if (generation !== voiceStartGeneration || voiceCallPhase.value !== 'outgoing') {
      try { await micTransceiver.sender.replaceTrack(null) } catch { /* ignore */ }
      track.stop()
      return
    }
    voiceCallPhase.value = 'connected'
    micOn.value = true
    if (voiceAudioRef.value) {
      voiceAudioRef.value.muted = voiceSpeakerMuted.value
      try { await voiceAudioRef.value.play() } catch {
        ElMessage.warning('请再次点击页面以允许播放通话声音')
      }
    }
    addLog(`语音通话已连接: ${track.label || 'audio'}`)
    ElMessage.success('语音通话已连接')
  } catch (err) {
    await hangUpVoiceCall(true)
    voiceCallReason.value = 'media_attach_failed'
    voiceCallPhase.value = 'error'
    addLog(`语音轨道附加失败: ${String(err)}`)
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
let standardSignaling: StandardRtcSignaling | null = null
let standardConfigTimer: number | null = null
let directFallbackTimer: number | null = null
let standardRestarting = false
const pendingStandardRemoteIce: RTCIceCandidateInit[] = []
let pingTimer: number | null = null
const pingRttMs = ref(-1)
let input: InputController | null = null

function decodeRtcIceConfig(encoded: string): RtcSessionIceConfig | null {
  if (!encoded) return null
  try {
    const padded = encoded.replace(/-/g, '+').replace(/_/g, '/')
      + '='.repeat((4 - (encoded.length % 4)) % 4)
    const binary = atob(padded)
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0))
    const value = JSON.parse(new TextDecoder().decode(bytes)) as RtcSessionIceConfig
    return Number.isFinite(value.revision) && Array.isArray(value.ice_servers) ? value : null
  } catch {
    return null
  }
}

function browserIceServers(config: RtcSessionIceConfig | null): RTCIceServer[] {
  return (config?.ice_servers ?? []).map((server) => ({
    urls: server.urls,
    username: server.username,
    credential: server.credential,
  }))
}

async function hydrateGuestRelayRoute(): Promise<void> {
  if (connectionTicket.value || (relayHost.value && relayPort.value > 0)) return
  try {
    const resp = await fetch('/get/render/configuration')
    const result = (await resp.json()) as {
      code?: number
      data?: { device_id?: string; relay_host?: string; relay_port?: number }
    }
    if (result.code !== 200 || !result.data) return
    if (result.data.relay_host) relayHost.value = result.data.relay_host
    if (Number(result.data.relay_port) > 0) relayPort.value = Number(result.data.relay_port)
    const deviceId = result.data.device_id || form.deviceId
    // A direct URL initially uses the public device id. Console Relay registers
    // the Render under its server_ identity, so the guest standard path must
    // replace (not merely fill) that initial value.
    if (deviceId) signalDeviceId.value = `server_${deviceId}`
    if (relayHost.value && relayPort.value > 0) {
      addLog('[rtc-route] 已加载游客标准 RTC Relay，认证仍只使用设备密码')
    }
  } catch (error) {
    addLog(`[rtc-route] 加载游客标准 RTC Relay 失败: ${String(error)}`)
  }
}

function isStandardRtc(): boolean {
  return forceStandardRtc || requestedConnectionType.value === 'rtc'
}

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
const lastClipboardAck = ref('')
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
const remoteFps = ref(0)
const virtualDisplayEnabled = ref(false)
const virtualDisplayOwnedCount = ref(0)
const virtualDisplayMaxCount = ref(2)
const virtualDisplayPending = ref(false)
const virtualDisplayGeneration = ref('0')
let virtualDisplayRequestSeq = 0

function scheduleTopologyReconnect(generation: string) {
  if (manualClose) return
  cancelReconnectTimer()
  status.value = 'reconnecting'
  setConnectStep('reconnect', `虚拟显示拓扑 generation=${generation}, 正在重新协商视频轨道`)
  addLog(`虚拟显示拓扑 generation=${generation} 已就绪，重新建立 WebRTC 会话`)
  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = null
    // Service 会重启 Render 让采集插件重新枚举显示器。第一次信令可能恰好
    // 落在旧进程退出/新进程监听之间；把它标记为自动重连流程，让 connect()
    // 的失败分支继续按统一退避策略重试，而不是一次 HTTP 502 后停住。
    reconnectCount.value = Math.max(reconnectCount.value, 1)
    void connect()
  }, 1500)
}

function requestVirtualDisplay(operation: 'create' | 'remove'): boolean {
  if (!virtualDisplayEnabled.value || virtualDisplayPending.value) return false
  const requestId = `web-${Date.now()}-${++virtualDisplayRequestSeq}`
  const sent = sendControl({
    type: MSG_TYPE_VIRTUAL_DISPLAY_REQUEST,
    virtualDisplayRequest: {
      requestId,
      operation: operation === 'create' ? 0 : 1,
      width: 1920,
      height: 1080,
      refreshHz: 60,
    },
  })
  if (!sent) {
    ElMessage.warning(t('float.dcNotReady'))
    return false
  }
  virtualDisplayPending.value = true
  addLog(`虚拟显示器请求已发送: ${operation}, requestId=${requestId}`)
  return true
}

// render 修复前(旧版本插件)会把每个编码视频帧也塞进 media_data_channel
// (px.Message type=kVideoFrame(30)/kAudioFrame(40), ~20KB×60fps),web 端不认识,
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
    } else if (msg.type === MSG_TYPE_CLIPBOARD_INFO_RESP) {
      const text = parseClipboardResponseText(payload)
      if (text !== null) {
        lastClipboardAck.value = text
        addLog(`远端剪贴板写入成功 (${text.length} 字符)`)
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
      if (typeof cfg.fps === 'number' && cfg.fps > 0) {
        remoteFps.value = cfg.fps
      }
      virtualDisplayEnabled.value = cfg.virtualDisplayEnabled ?? false
      virtualDisplayOwnedCount.value = cfg.virtualDisplayOwnedCount ?? 0
      virtualDisplayMaxCount.value = cfg.virtualDisplayMaxCount || 2
      virtualDisplayGeneration.value = String(cfg.topologyGeneration ?? 0)
      const remoteVoiceSupported = (cfg.voiceCallEnabled ?? false)
        && (cfg.voiceCallProtocolVersion ?? 0) >= 1
        && hasGrantedPermission('audio')
      voiceCallSupported.value = remoteVoiceSupported && voiceBrowserMediaAvailable
      if (remoteVoiceSupported && !voiceBrowserMediaAvailable) {
        voiceCallReason.value = t('float.voiceSecureContextRequired')
        addLog('Web语音不可用：浏览器麦克风要求HTTPS或localhost安全上下文')
      } else if (voiceCallPhase.value === 'idle') {
        voiceCallReason.value = ''
      }
      voiceCallRequiresHeadset.value = cfg.voiceCallRequiresHeadset ?? false
      addLog(`收到远端显示器配置: ${remoteMonitors.value.length} 个显示器, 采集 ${capturingMonitor.value}, fps=${remoteFps.value || '-'}`)
      // FT 协议版本门控:rustdesk 语义 = 2;旧版被控(缺省/0)不兼容,入口置灰
      ftProtocolVersion.value = cfg.ftProtocolVersion ?? 0
      if (!ftSupported.value) {
        addLog(`对端文件传输协议版本不兼容(ftProtocolVersion=${ftProtocolVersion.value}),文件传输不可用`)
      }
      if (voiceCallSupported.value) {
        addLog(`对端支持语音通话(v${cfg.voiceCallProtocolVersion ?? 0})`)
      }
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
    } else if (msg.type === MSG_TYPE_VIRTUAL_DISPLAY_RESPONSE && msg.virtualDisplayResponse) {
      const r = msg.virtualDisplayResponse
      virtualDisplayPending.value = false
      if (!r.accepted || r.state === 2) {
        const detail = `${r.errorCode || 'VIRTUAL_DISPLAY_FAILED'}: ${r.errorMessage || 'unknown error'}`
        addLog(`虚拟显示器请求失败: ${detail}`)
        ElMessage.error(detail)
      } else {
        // A failed response carries protobuf defaults for count/generation.
        // Preserve the last confirmed topology so the UI and E2E gate cannot
        // mistake a partial driver failure for a successful removal.
        virtualDisplayOwnedCount.value = r.ownedDisplayCount
        virtualDisplayGeneration.value = String(r.topologyGeneration ?? 0)
        addLog(`虚拟显示器请求完成: owned=${r.ownedDisplayCount}, state=${r.state}, generation=${virtualDisplayGeneration.value}`)
        if (r.state === 1) {
          scheduleTopologyReconnect(virtualDisplayGeneration.value)
        }
      }
    } else if (msg.type === MSG_TYPE_VOICE_CALL_RESPONSE && msg.voiceCallResponse) {
      void handleVoiceCallResponse(msg.voiceCallResponse)
    } else if (msg.type === MSG_TYPE_VOICE_CALL_REQUEST && msg.voiceCallRequest &&
               !msg.voiceCallRequest.connect &&
               matchesActiveVoiceCall(currentVoiceIdentity(), msg.voiceCallRequest.callId)) {
      void finishVoiceCall('remote_hangup').then(() => {
        ElMessage.info('被控端已结束语音通话')
      })
    } else if (msg.type === MSG_TYPE_VOICE_AUDIO_CONFIG && msg.voiceAudioConfig &&
               matchesActiveVoiceCall(currentVoiceIdentity(), msg.voiceAudioConfig.callId)) {
      const config = msg.voiceAudioConfig
      if (!isSupportedVoiceAudioConfig(currentVoiceIdentity(), config)) {
        addLog(`语音媒体参数不兼容: ${config.sampleRate}/${config.channels}/${config.frameMs}`)
        void hangUpVoiceCall(true)
        ElMessage.error('被控端语音媒体参数不兼容')
      }
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
    } else if (msg.type === MSG_TYPE_CONNECTION_TAKEN_OVER) {
      // 被其它客户端接管:主机踢人前会先发这条消息。按手动断开处理
      // (置 manualClose,阻止 connectionstatechange 触发自动重连),并明确提示
      addLog('连接已被其它客户端接管')
      disconnect()
      ElMessageBox.alert(
        '连接已被其它客户端接管。',
        '连接已断开',
        { confirmButtonText: '知道了', type: 'warning' },
      ).catch(() => {})
    } else if (msg.type === MSG_TYPE_INSTANCE_STOPPED && msg.instanceStopped) {
      // 实例被 Console 停止:render 退出前广播。按手动断开处理(置 manualClose,
      // 阻止自动重连),并明确提示
      addLog(`实例已被停止 (${msg.instanceStopped.reason || '-'})`)
      disconnect()
      ElMessageBox.alert(
        '实例已被停止。',
        '连接已断开',
        { confirmButtonText: '知道了', type: 'warning' },
      ).catch(() => {})
    } else if (msg.type === MSG_TYPE_GAME_STATUS_CHANGED && msg.gameStatusChanged) {
      // game-hook 游戏状态:0=运行/恢复, 2=死亡后看门狗重启中
      const g = msg.gameStatusChanged
      if (g.status === 2) {
        addLog(`游戏异常退出,正在自动重启 (${g.detail || '-'})`)
        ElNotification.closeAll()
        ElNotification({
          title: '游戏重启中',
          message: '游戏异常退出，正在自动重启，画面将在游戏恢复后继续…',
          type: 'warning',
          duration: 0,
        })
      } else if (g.status === 0) {
        addLog(`游戏已恢复 (${g.detail || '-'})`)
        ElNotification.closeAll()
        ElNotification({
          title: '游戏已恢复',
          message: '游戏已重启完成，画面恢复。',
          type: 'success',
          duration: 5000,
        })
      }
    }
    // 其余二进制控制消息(统计等)暂不处理
  }
}

// 「发送到远端」:读本地剪贴板 -> kClipboardInfo -> px_user_proxy 写入远端系统剪贴板
async function sendClipboardToRemote(): Promise<boolean> {
  if (!hasGrantedPermission('clipboard')) {
    addLog('当前连接票据未授予剪贴板权限')
    return false
  }
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
  if (!hasGrantedPermission('clipboard')) return false
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
    sendText: (text: string) => hasGrantedPermission('clipboard')
      && sendClipboardText(dc, form.deviceId, form.streamId, text),
    lastRemote: () => remoteClipboard.value,
    lastAck: () => lastClipboardAck.value,
  }
  w.__perf = () => ({ ...perf.value })
  w.__mic = {
    toggle: () => toggleVoiceCall(),
    on: () => micOn.value,
    phase: () => voiceCallPhase.value,
    supported: () => voiceCallSupported.value,
    mute: () => toggleVoiceMute(),
    speakerMute: () => toggleVoiceSpeakerMute(),
    state: () => ({
      phase: voiceCallPhase.value,
      supported: voiceCallSupported.value,
      reason: voiceCallReason.value,
      browserMediaAvailable: voiceBrowserMediaAvailable,
      requiresHeadset: voiceCallRequiresHeadset.value,
      preflightShown: voicePreflightShown,
      preflightPending: voicePreflightPending,
      micMuted: voiceMicMuted.value,
      speakerMuted: voiceSpeakerMuted.value,
      senderTrack: micTransceiver?.sender.track?.readyState ?? 'none',
      localTracks: micStream?.getTracks().map((track) => ({
        kind: track.kind,
        enabled: track.enabled,
        readyState: track.readyState,
      })) ?? [],
      lastMicTrackStopState,
      voiceOutputMuted: voiceAudioRef.value?.muted ?? null,
      voiceOutputTracks: (voiceAudioRef.value?.srcObject as MediaStream | null)
        ?.getTracks().map((track) => ({ kind: track.kind, readyState: track.readyState })) ?? [],
      systemOutputMuted: videoRef.value?.muted ?? null,
      systemAudioTrackCount: (videoRef.value?.srcObject as MediaStream | null)
        ?.getAudioTracks().length ?? 0,
    }),
  }
}

// ---------- 文件传输(ft_data_channel)----------
// 状态与操作在 useFileTransfer composable;UI 在 FileTransferWindow.vue
let ftDc: RTCDataChannel | null = null
const ftVisible = ref(false)
const ft = useFileTransfer()
const ftReady = ft.ftReady
// FT 协议版本(kServerConfiguration.ftProtocolVersion;rustdesk 语义 = 2):
// null = 尚未收到配置;旧版被控(0/缺省)与新版不互通,入口置灰 + 提示(plan §0 版本门控)
const ftProtocolVersion = ref<number | null>(null)
const ftSupported = computed(() => ftProtocolVersion.value === 2)

// 无头/CDP 调试用:window.__ft
function exposeFtDebug() {
  const w = window as unknown as { __ft?: unknown }
  w.__ft = {
    ready: () => ft.ftReady.value,
    supported: () => ftSupported.value,
    listDir: (path: string) => ft.client()?.listDir(path),
    uploadText: async (name: string, targetDir: string, content: string) => {
      const client = ft.client()
      if (!client) throw new Error('ft not ready')
      const bytes = new TextEncoder().encode(content)
      const file = new File([bytes.slice().buffer], name)
      const job = ft.uploadFile(file, targetDir)
      return { jobId: job.id, sha256: await sha256Hex(bytes), size: bytes.length }
    },
    download: async (path: string) => {
      const files = await ft.downloadToMemory(path)
      const f = files[0]
      if (!f) throw new Error('no file received')
      return { name: f.name, size: f.size, sha256: await sha256Hex(f.data) }
    },
    removeFile: (path: string) => {
      const client = ft.client()
      if (!client) throw new Error('ft not ready')
      return client.removeFile(path)
    },
    createDir: (path: string) => {
      const client = ft.client()
      if (!client) throw new Error('ft not ready')
      return client.createDir(path)
    },
    jobs: () => ft.client()?.getJobs() ?? [],
  }
}

// 压低 Chrome 视频抖动缓冲/播放余量(跟手性关键)。
// playoutDelayHint=0:关掉自适应播放延迟;jitterBufferTarget=0:把目标缓冲钉到 0ms。
// ontrack 时轨经常仍是 muted,此时赋值可能不生效;必须在 unmute / 连通后再设。
// 不支持的浏览器属性不存在,静默忽略。
type LowLatencyReceiver = RTCRtpReceiver & {
  playoutDelayHint?: number
  jitterBufferTarget?: number
}
let lowLatencyTimer: number | null = null
function applyLowLatencyPlayout(receiver: RTCRtpReceiver, track?: MediaStreamTrack) {
  const apply = () => {
    try {
      const r = receiver as LowLatencyReceiver
      r.playoutDelayHint = 0
      r.jitterBufferTarget = 0
      if (track && 'contentHint' in track) {
        // motion: 优先低延迟,允许更多压缩伪影
        ;(track as MediaStreamTrack & { contentHint?: string }).contentHint = 'motion'
      }
    } catch {
      /* ignore */
    }
  }
  apply()
  // readyState===live 但 muted===true 是常见态,旧逻辑漏了 unmute 重试
  if (track?.muted) {
    track.addEventListener('unmute', apply, { once: true })
  }
}
function startLowLatencyKeepalive(pc: RTCPeerConnection) {
  if (lowLatencyTimer != null) {
    window.clearInterval(lowLatencyTimer)
  }
  const tick = () => {
    for (const receiver of pc.getReceivers()) {
      if (receiver.track?.kind === 'video') {
        applyLowLatencyPlayout(receiver, receiver.track)
      }
    }
  }
  tick()
  // Chrome 偶发把 target 拉回 ~1s;连通期间高频钉死为 0
  lowLatencyTimer = window.setInterval(tick, 250)
}
function stopLowLatencyKeepalive() {
  if (lowLatencyTimer != null) {
    window.clearInterval(lowLatencyTimer)
    lowLatencyTimer = null
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
  if (!v && !hasGrantedPermission('input')) {
    viewOnly.value = true
    return
  }
  if (input) input.viewOnly = v
})

watch(pointerLocked, (v) => {
  input?.setRelativeMode(v)
})

const logBodyRef = ref<HTMLElement | null>(null)

function addLog(msg: string) {
  const line = `[${new Date().toLocaleTimeString()}] ${msg}`
  console.log(line)
  logs.value.push(line)
  while (logs.value.length > MAX_LOG_LINES) {
    logs.value.shift()
  }
  void nextTick(() => {
    const el = logBodyRef.value
    if (el) el.scrollTop = el.scrollHeight
  })
}

/** 连接流程步骤(与 connect() 实际阶段对齐,供加载页与 console 诊断) */
type ConnectStep =
  | 'idle'
  | 'init'
  | 'negotiate'
  | 'ice'
  | 'signal'
  | 'answer'
  | 'peer'
  | 'channels'
  | 'video'
  | 'reconnect'
  | 'failed'
  | 'done'

const CONNECT_FLOW_STEPS: ConnectStep[] = [
  'init',
  'negotiate',
  'ice',
  'signal',
  'answer',
  'peer',
  'channels',
  'video',
]

const connectStep = ref<ConnectStep>('idle')
const connectStepDetail = ref('')

function setConnectStep(step: ConnectStep, detail = '') {
  connectStep.value = step
  connectStepDetail.value = detail
  const idx = CONNECT_FLOW_STEPS.indexOf(step)
  const progress =
    idx >= 0 ? ` [${idx + 1}/${CONNECT_FLOW_STEPS.length}]` : ''
  const label = t(`loading.steps.${step}`)
  const line = detail
    ? `[connect]${progress} ${label} — ${detail}`
    : `[connect]${progress} ${label}`
  // addLog 同步写日志面板并 console.log,便于出问题后在 DevTools 定位
  addLog(line)
}

const connectStepIndex = computed(() => CONNECT_FLOW_STEPS.indexOf(connectStep.value))

function stepItemState(step: ConnectStep): 'done' | 'active' | 'pending' {
  if (connectStep.value === 'failed') {
    const failAt = connectStepIndex.value
    const i = CONNECT_FLOW_STEPS.indexOf(step)
    if (failAt < 0) return 'pending'
    if (i < failAt) return 'done'
    if (i === failAt) return 'active'
    return 'pending'
  }
  if (connectStep.value === 'done' || (status.value === 'connected' && hasVideo.value)) {
    return 'done'
  }
  const cur = connectStepIndex.value
  const i = CONNECT_FLOW_STEPS.indexOf(step)
  if (cur < 0) return 'pending'
  if (i < cur) return 'done'
  if (i === cur) return 'active'
  return 'pending'
}

function clearLogs() {
  logs.value = []
}

function selectAllLogs() {
  const el = logBodyRef.value
  if (!el) return
  const range = document.createRange()
  range.selectNodeContents(el)
  const sel = window.getSelection()
  sel?.removeAllRanges()
  sel?.addRange(range)
}

function deselectAllLogs() {
  window.getSelection()?.removeAllRanges()
}

const showLoading = computed(
  () =>
    status.value === 'idle' ||
    status.value === 'connecting' ||
    status.value === 'reconnecting' ||
    status.value === 'failed' ||
    (status.value === 'connected' && !hasVideo.value),
)

const loadingHint = computed(() => {
  if (status.value === 'failed') return errorMsg.value || t('status.failed')
  if (status.value === 'idle') return t('loading.idleHint')
  if (connectStepDetail.value) return connectStepDetail.value
  if (connectStep.value && connectStep.value !== 'idle') {
    return t(`loading.steps.${connectStep.value}`)
  }
  return t('loading.hint')
})

const showStepList = computed(
  () =>
    status.value === 'connecting' ||
    status.value === 'reconnecting' ||
    status.value === 'failed' ||
    (status.value === 'connected' && !hasVideo.value),
)

// 从 URL query 带入参数:
//   推荐:?c=<URL-safe Base64 JSON{d,p?,m?}> (panel/Console 生成,避免明文密码)
//   兼容:?deviceId=&password= 或 &pwd_md5=(预哈希),便于本地调试
// deviceId 出现在 URL 时自动连接;密码可空(render 未设安全密码时放行)
// 流 ID 由设备 ID 派生,不从 URL 带入
const pwdMd5Override = ref('')
/** true when URL/token explicitly supplied a device id (triggers auto-connect) */
const autoConnectFromUrl = ref(false)
const signalDeviceId = ref('')

function loadQueryParams() {
  const q = new URLSearchParams(window.location.search)
  const fragment = new URLSearchParams(window.location.hash.replace(/^#/, ''))
  const token = q.get('c')
  if (token) {
    const decoded = decodeConnectToken(token)
    if (decoded) {
      form.deviceId = decoded.deviceId
      form.password = decoded.password
      pwdMd5Override.value = decoded.pwdMd5
      autoConnectFromUrl.value = !!decoded.deviceId
      addLog(`[connect] 已从 ?c= 解码连接参数 (deviceId=${decoded.deviceId})`)
    } else {
      addLog('[connect] ?c= 参数解码失败,将尝试明文 query')
    }
  }
  // 明文 query 可覆盖/补齐(调试用);token 已填时不再被空明文冲掉
  if (!form.deviceId) form.deviceId = q.get('deviceId') ?? ''
  if (!form.password) form.password = q.get('password') ?? ''
  if (!pwdMd5Override.value) pwdMd5Override.value = q.get('pwd_md5') ?? ''
  connectionTicket.value = fragment.get('ticket') ?? ''
  renewalToken.value = fragment.get('renew') ?? ''
  renewalUrl.value = fragment.get('renew_url') ?? ''
  requestedConnectionType.value = q.get('connType') === 'rtc' ? 'rtc' : 'rtc_direct'
  relayHost.value = fragment.get('relay_host') ?? ''
  relayPort.value = Number(fragment.get('relay_port') ?? 0)
  signalDeviceId.value = fragment.get('signal_device_id') ?? form.deviceId
  rtcIceConfig = decodeRtcIceConfig(fragment.get('ice') ?? '')
  if (requestedConnectionType.value === 'rtc' && (!rtcIceConfig || !relayHost.value || relayPort.value <= 0)) {
    addLog('[rtc-standard] Console 启动参数不完整，将在连接时明确报错')
  } else if (rtcIceConfig) {
    addLog(`[rtc-route] requested=${requestedConnectionType.value} ICE revision=${rtcIceConfig.revision}`)
  }
  grantedPermissions.value = (fragment.get('perms') ?? '')
    .split(',')
    .map((permission) => permission.trim())
    .filter(Boolean)
  if (grantedPermissions.value.length > 0 && !grantedPermissions.value.includes('input')) {
    viewOnly.value = true
  }
  const instanceId = fragment.get('instance') ?? q.get('instanceId') ?? ''
  connectionInstanceId.value = instanceId
  if (instanceId) {
    addLog(`[connect] instanceId=${instanceId}`)
  }
  ensureClientNonce(fragment.get('nonce') ?? q.get('nonce') ?? '')
  if (connectionTicket.value) {
    addLog('[connect] 已加载 Console 一次性连接票据')
    // Remove secrets from the address bar/history after parsing. The in-memory
    // copy remains available for the immediately following signaling request.
    history.replaceState(null, '', `${window.location.pathname}${window.location.search}`)
  }
  if (q.get('deviceId') || q.get('c')) {
    autoConnectFromUrl.value = !!form.deviceId
  }
  // URL 未带设备 ID 时用上次成功连接的设备 ID 预填(不存密码,不自动连)
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
  if (directFallbackTimer !== null) {
    window.clearTimeout(directFallbackTimer)
    directFallbackTimer = null
  }
  if (standardConfigTimer !== null) {
    window.clearInterval(standardConfigTimer)
    standardConfigTimer = null
  }
  standardRestarting = false
  pendingStandardRemoteIce.length = 0
  standardSignaling?.stop()
  standardSignaling = null
  input?.detach()
  input = null
  gamepad?.disable()
  gamepadOn.value = false
  if (document.pointerLockElement) document.exitPointerLock()
  perfCollector.stop()
  stopLowLatencyKeepalive()
  perf.value = { ...EMPTY_PERF }
  remoteClipboard.value = ''
  lastClipboardAck.value = ''
  remoteMonitors.value = []
  capturingMonitor.value = ''
  remoteFps.value = 0
  virtualDisplayPending.value = false
  clearVoiceTimeout()
  micStream?.getTracks().forEach((t) => t.stop())
  micStream = null
  micOn.value = false
  voiceMicMuted.value = false
  voiceSpeakerMuted.value = false
  voiceCallPhase.value = 'idle'
  voiceCallReason.value = ''
  voicePreflightShown = false
  voicePreflightPending = false
  voiceCallId = ''
  voiceRequestId = ''
  ++voiceStartGeneration
  voiceCallSupported.value = false
  micTransceiver = null
  const w = window as unknown as { __pc?: RTCPeerConnection | null }
  w.__pc = null
  if (ftDc) {
    ft.resetFt('连接已断开')
    ftProtocolVersion.value = null
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
  if (voiceAudioRef.value) voiceAudioRef.value.srcObject = null
}

async function restartStandardRtc(reason: string, force = false) {
  if (!pc || !standardSignaling || standardRestarting || manualClose) return
  standardRestarting = true
  const previousRevision = rtcIceConfig?.revision ?? 0
  try {
    let nextConfig = rtcIceConfig
    if (connectionTicket.value) {
      const renewed = await renewConnectionTicket(false)
      nextConfig = renewed.rtcIceConfig ?? rtcIceConfig
      if (!nextConfig) throw new Error('Console 未返回新的 ICE 配置')
      if (!force && nextConfig.revision <= previousRevision) return
      rtcIceConfig = nextConfig
    } else if (!force) {
      // Guest sessions have no Console configuration revision to poll.
      return
    }
    pc.setConfiguration({
      iceServers: browserIceServers(nextConfig),
      iceTransportPolicy: 'all',
    })
    pendingStandardRemoteIce.length = 0
    const offer = await pc.createOffer({ iceRestart: true })
    await pc.setLocalDescription(offer)
    if (!offer.sdp) throw new Error('ICE restart Offer SDP 为空')
    ticketConsumed = !!connectionTicket.value
    const answer = await standardSignaling.exchangeOffer(
      offer.sdp,
      connectionTicket.value,
      clientNonce.value,
      connectionInstanceId.value,
      effectivePwdMd5(),
    )
    await pc.setRemoteDescription({ type: 'answer', sdp: answer })
    for (const candidate of pendingStandardRemoteIce.splice(0)) {
      await pc.addIceCandidate(candidate)
    }
    addLog(`[rtc-standard] SetConfiguration + ICE restart 完成: reason=${reason} revision=${nextConfig?.revision ?? 'guest'}`)
  } catch (error) {
    addLog(`[rtc-standard] ICE restart 失败: ${String(error)}`)
    scheduleReconnect('ICE restart failed')
  } finally {
    standardRestarting = false
  }
}

function startStandardRtcConfigPolling() {
  if (standardConfigTimer !== null) window.clearInterval(standardConfigTimer)
  standardConfigTimer = window.setInterval(() => {
    if (pc?.connectionState === 'connected' && standardSignaling && !standardRestarting) {
      void restartStandardRtc('configuration poll', false)
    }
  }, STANDARD_RTC_CONFIG_POLL_MS)
}

async function connect() {
  if (!form.deviceId) {
    errorMsg.value = '请填写设备 ID'
    status.value = 'failed'
    setConnectStep('init', errorMsg.value)
    return
  }
  cleanup()
  status.value = 'connecting'
  errorMsg.value = ''
  setConnectStep('init', `deviceId=${form.deviceId} streamId=${form.streamId}`)

  try {
    if (!connectionTicket.value
      && (forceStandardRtc || requestedConnectionType.value === 'rtc')) {
      await hydrateGuestRelayRoute()
    }
    const standardRtc = isStandardRtc()
    if (standardRtc) {
      if (!relayHost.value || relayPort.value <= 0) {
        throw new Error('RTC Standard 缺少 Relay 启动参数')
      }
      if (connectionTicket.value && !rtcIceConfig) {
        throw new Error('已登录 RTC Standard 缺少 Console ICE 配置')
      }
      // A Direct attempt may already have consumed the one-time ticket at the
      // Render. Rotate it before opening the authenticated signaling socket.
      if (connectionTicket.value && ticketConsumed) await renewConnectionTicket()
      pc = new RTCPeerConnection({
        iceServers: browserIceServers(rtcIceConfig),
        iceTransportPolicy: 'all',
      })
      addLog(`[rtc-route] RTC Standard, ICE revision=${rtcIceConfig?.revision ?? 'guest'}`)
    } else {
      // Direct RTC remains host-only; Render rewrites its candidate to the
      // browser-reachable address for the specialized local endpoint.
      pc = new RTCPeerConnection()
      addLog('[rtc-route] RTC Direct')
    }
    // 无头/CDP 调试用:getStats 等诊断入口
    ;(window as unknown as { __pc?: RTCPeerConnection | null }).__pc = pc

    if (standardRtc) {
      pc.onicecandidate = (event) => {
        if (event.candidate) standardSignaling?.sendIce(event.candidate)
      }
      standardSignaling = new StandardRtcSignaling(
        {
          relayHost: relayHost.value,
          relayPort: relayPort.value,
          remoteDeviceId: signalDeviceId.value || form.deviceId,
          ticketDeviceId: form.deviceId,
          streamId: form.streamId,
          ticket: connectionTicket.value,
          clientNonce: clientNonce.value,
          instanceId: connectionInstanceId.value,
          safetyPwdMd5: connectionTicket.value ? '' : effectivePwdMd5(),
          secure: window.location.protocol === 'https:',
        },
        async (candidate) => {
          if (pc?.remoteDescription) await pc.addIceCandidate(candidate)
          else pendingStandardRemoteIce.push(candidate)
        },
        addLog,
      )
    }

    // 第一条音频 m-line 只接收桌面系统声；第二条是语音通话专用
    // sendrecv 轨。两流独立，语音挂断不会破坏系统声音。
    pc.addTransceiver('audio', { direction: 'recvonly' })
    micTransceiver = pc.addTransceiver('audio', { direction: 'sendrecv' })

    pc.ontrack = (ev: RTCTrackEvent) => {
      applyLowLatencyPlayout(ev.receiver, ev.track)
      const isVoiceTrack = ev.track.kind === 'audio' && (
        ev.track.id === 'voice_call_audio'
        || ev.streams.some((stream) => stream.id === 'pixels_voice_call')
      )
      if (isVoiceTrack) {
        if (voiceAudioRef.value) {
          voiceAudioRef.value.srcObject = new MediaStream([ev.track])
          voiceAudioRef.value.muted = voiceSpeakerMuted.value
        }
        addLog(`ontrack: independent voice call track ${ev.track.id}`)
        return
      }
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
        setConnectStep('done', `video track ${ev.track.id}`)
      } else if (!hasVideo.value) {
        setConnectStep('video', `收到 ${ev.track.kind} 轨,仍等待视频`)
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
      if (state === 'connecting') {
        setConnectStep('peer', `connectionState=${state}`)
      } else if (state === 'connected') {
        if (directFallbackTimer !== null) {
          window.clearTimeout(directFallbackTimer)
          directFallbackTimer = null
        }
        // 连接(或重连)成功:取消挂起的重连、清零重试计数
        cancelReconnectTimer()
        reconnectCount.value = 0
        manualClose = false
        status.value = 'connected'
        if (!hasVideo.value) {
          setConnectStep('channels', 'P2P 已连通,等待通道与画面')
        }
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
          // 连接就绪后持续压播放延迟(ontrack 时常 muted,单次赋值不可靠)
          startLowLatencyKeepalive(pc)
          perfCollector.start(pc)
        }
        startConnWatchdog()
        if (standardRtc && connectionTicket.value) startStandardRtcConfigPolling()
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
      if (!hasVideo.value) {
        setConnectStep('video', `控制通道 ${DATA_CHANNEL_LABEL} 已打开`)
      }
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
      if (!hasGrantedPermission('file')) {
        addLog('当前连接票据未授予文件传输权限,关闭文件通道')
        ftDc?.close()
        return
      }
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
      if (!hasGrantedPermission('input')) {
        addLog('当前连接票据未授予输入权限,关闭输入通道')
        inputDc?.close()
        return
      }
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

    setConnectStep('negotiate')
    if (standardRtc) {
      setConnectStep('signal', '正在连接 Console 标准 RTC 信令 Relay')
      await standardSignaling?.connect()
    }
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

    if (standardRtc) {
      const localSdp = pc.localDescription?.sdp
      if (!localSdp || !standardSignaling) throw new Error('标准 RTC 本地 SDP 或信令对象为空')
      setConnectStep('signal', '通过 Console Relay 交换 SDP/Trickle ICE')
      ticketConsumed = !!connectionTicket.value
      const answerSdp = await standardSignaling.exchangeOffer(
        localSdp,
        connectionTicket.value,
        clientNonce.value,
        connectionInstanceId.value,
        effectivePwdMd5(),
      )
      setConnectStep('answer', `standard answer_sdp length=${answerSdp.length}`)
      scanFr('answer', answerSdp)
      await pc.setRemoteDescription({ type: 'answer', sdp: answerSdp })
      for (const candidate of pendingStandardRemoteIce.splice(0)) {
        await pc.addIceCandidate(candidate)
      }
      setConnectStep('peer', `connectionState=${pc.connectionState} ice=${pc.iceConnectionState}`)
      return
    }

    // 等 ICE gathering complete 再发 offer,不做 trickle
    setConnectStep('ice', `iceGatheringState=${pc.iceGatheringState}`)
    await waitIceGatheringComplete(pc)
    setConnectStep('signal', 'ICE 完成,准备 POST /alloc/local/rtc')

    const localDesc = pc.localDescription
    if (!localDesc?.sdp) throw new Error('本地 SDP 为空')

    // 发信令拿 answer;返回空字符串表示"连接被占用"(code 704),由调用方决定接管或放弃
    const postSignal = async (takeover: boolean): Promise<string> => {
      if (connectionTicket.value && ticketConsumed) {
        await renewConnectionTicket()
      }
      const query = new URLSearchParams({
        device_id: form.deviceId,
        stream_id: form.streamId,
        safety_pwd_md5: effectivePwdMd5(),
      })
      if (clientNonce.value) query.set('client_nonce', clientNonce.value)
      if (takeover) query.set('takeover', '1')
      setConnectStep('signal', takeover ? 'takeover=1 重新请求信令' : 'POST 信令中')
      const body: Record<string, string> = { sdp: localDesc.sdp }
      if (connectionTicket.value) {
        body.ticket = connectionTicket.value
        body.client_nonce = clientNonce.value
        body.instance_id = connectionInstanceId.value
        // The Render may redeem the ticket even when the HTTP response is lost,
        // so every subsequent signaling attempt must rotate it first.
        ticketConsumed = true
      }
      const resp = await fetch(`${SIGNAL_URL}?${query.toString()}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      })
      if (!resp.ok) {
        const detail = await resp.text().catch(() => '')
        throw new Error(`信令请求失败: HTTP ${resp.status}${detail ? ` — ${detail.slice(0, 300)}` : ''}`)
      }
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

    setConnectStep('answer', `answer_sdp length=${answerSdp.length}`)
    scanFr('answer', answerSdp)
    await pc.setRemoteDescription({ type: 'answer', sdp: answerSdp })
    setConnectStep('peer', `connectionState=${pc.connectionState} ice=${pc.iceConnectionState}`)
    // A successful HTTP probe/answer is not yet a connected Direct session.
    // If ICE/data channels never become ready, rotate the ticket and reopen as
    // RTC Standard instead of retrying the same local path forever.
    directFallbackTimer = window.setTimeout(() => {
      if (pc && pc.connectionState !== 'connected' && !manualClose) {
        forceStandardRtc = true
        addLog('[rtc-route] Direct SDP 成功但实际建连超时，自动重开 RTC Standard')
        scheduleReconnect('direct connect timeout')
      }
    }, 10000)
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err)
    addLog(`连接失败: ${msg}`)
    const failedAt = CONNECT_FLOW_STEPS.includes(connectStep.value) ? connectStep.value : 'init'
    cleanup()
    if (!manualClose && reconnectCount.value > 0) {
      // 自动重连过程中的失败:计入重试,继续排队下一次
      scheduleReconnect(`重连失败(${msg})`)
    } else {
      status.value = 'failed'
      errorMsg.value = msg
      setConnectStep(failedAt, msg)
    }
  }
}

// 手动点「连接/重新连接」:清零重试计数
function manualConnect() {
  manualClose = false
  forceStandardRtc = false
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
  connectStep.value = 'idle'
  connectStepDetail.value = ''
  addLog('已断开')
}

// 无头/CDP 调试用:window.__input / window.__conn / window.__gamepad
function exposeInputConnDebug() {
  const w = window as unknown as {
    __input?: unknown
    __conn?: unknown
    __gamepad?: unknown
    __virtualDisplay?: unknown
  }
  w.__input = {
    lastMouse: () => input?.lastMouse ?? null,
    relative: () => input?.relativeMode ?? false,
    virtualPos: () => input?.virtualPos() ?? null,
    attached: () => !!input,
    testSend: (opts?: { x?: number; y?: number; keyCode?: string }) =>
      input?.testSend(opts) ?? { ok: false, reason: 'input not ready' },
  }
  w.__conn = {
    status: () => status.value,
    reconnectCount: () => reconnectCount.value,
    pointerLocked: () => pointerLocked.value,
    rtcMode: () => isStandardRtc() ? 'standard' : 'direct',
    iceRevision: () => rtcIceConfig?.revision ?? 0,
    restartIce: () => restartStandardRtc('diagnostic request', true),
    selectedPath: () => ({
      local: perf.value.localCand,
      remote: perf.value.remoteCand,
      rttMs: perf.value.rttMs,
    }),
  }
  w.__virtualDisplay = {
    state: () => ({
      enabled: virtualDisplayEnabled.value,
      owned: virtualDisplayOwnedCount.value,
      maximum: virtualDisplayMaxCount.value,
      generation: virtualDisplayGeneration.value,
      pending: virtualDisplayPending.value,
      monitors: remoteMonitors.value.map((m) => ({
        name: m.name,
        width: m.currentWidth,
        height: m.currentHeight,
        primary: m.primary,
      })),
      capturingMonitor: capturingMonitor.value,
      connection: status.value,
    }),
    create: () => requestVirtualDisplay('create'),
    remove: () => requestVirtualDisplay('remove'),
    switchMonitor: (name: string) =>
      sendControl({ type: 170, switchMonitor: { name } }),
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
  // A guest starts with Direct RTC and may need the same password-authenticated
  // standard RTC path as a fallback. Relay discovery does not issue a ticket.
  if (!connectionTicket.value) void hydrateGuestRelayRoute()
  document.addEventListener('pointerlockchange', onPointerLockChange)
  // URL 带了 deviceId/?c= 则自动连接(空密码也可,便于无头/本地调试)
  if (autoConnectFromUrl.value && form.deviceId) {
    addLog('[connect] URL 参数就绪,自动连接')
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
    <video
      ref="videoRef"
      class="remote-video"
      tabindex="0"
      autoplay
      playsinline
      :muted="muted"
      :class="{ hidden: !hasVideo }"
    ></video>

    <!-- 连接/等画面/失败加载页(无顶部参数条,参数由 URL 带入) -->
    <div v-if="showLoading" class="loading-page">
      <img class="loading-logo" :src="logoUrl" alt="Pixels" />
      <div class="loading-title">
        {{ status === 'failed' ? t('status.failed') : t('loading.title') }}
      </div>
      <div class="loading-hint" :class="{ error: status === 'failed' }">{{ loadingHint }}</div>
      <div
        v-if="showStepList && connectStepIndex >= 0"
        class="loading-step-meta"
      >
        {{ t('loading.stepProgress', { current: connectStepIndex + 1, total: CONNECT_FLOW_STEPS.length }) }}
        · {{ t(`loading.steps.${connectStep}`) }}
      </div>
      <ol v-if="showStepList" class="loading-steps">
        <li
          v-for="step in CONNECT_FLOW_STEPS"
          :key="step"
          class="loading-step"
          :class="stepItemState(step)"
        >
          <span class="step-mark" />
          <span class="step-text">{{ t(`loading.steps.${step}`) }}</span>
        </li>
      </ol>
      <div v-if="status !== 'failed' && status !== 'idle'" class="loading-spinner" />
      <button
        v-if="status === 'failed' || status === 'idle'"
        type="button"
        class="loading-action"
        @click="manualConnect"
      >
        {{ status === 'failed' ? t('app.reconnect') : t('app.connect') }}
      </button>
    </div>

    <FloatBall
      v-model:muted="muted"
      v-model:view-only="viewOnly"
      v-model:ft-visible="ftVisible"
      v-model:perf-visible="perfVisible"
      v-model:log-visible="logVisible"
      :connected="status === 'connected'"
      :can-disconnect="status === 'connected' || status === 'connecting' || status === 'reconnecting'"
      :ft-ready="ftReady"
      :ft-supported="ftSupported && hasGrantedPermission('file')"
      :perf="perf"
      :remote-fps="remoteFps"
      :clipboard-available="clipboardAvailable && hasGrantedPermission('clipboard')"
      :remote-clipboard="remoteClipboard"
      :send="sendControl"
      :send-clipboard-to-remote="sendClipboardToRemote"
      :copy-remote-to-local="copyRemoteToLocal"
      :voice-call-supported="voiceCallSupported"
      :voice-call-phase="voiceCallPhase"
      :voice-call-reason="voiceCallReason"
      :voice-call-requires-headset="voiceCallRequiresHeadset"
      :voice-mic-muted="voiceMicMuted"
      :voice-speaker-muted="voiceSpeakerMuted"
      :toggle-voice-call="toggleVoiceCall"
      :toggle-voice-mute="toggleVoiceMute"
      :toggle-voice-speaker-mute="toggleVoiceSpeakerMute"
      :get-video="() => videoRef"
      :pointer-locked="pointerLocked"
      :gamepad-on="gamepadOn"
      :gamepad-status="gamepadStatus"
      :toggle-gamepad="toggleGamepad"
      :monitors="remoteMonitors"
      :capturing-monitor="capturingMonitor"
      :virtual-display-enabled="virtualDisplayEnabled && hasGrantedPermission('input')"
      :virtual-display-owned-count="virtualDisplayOwnedCount"
      :virtual-display-max-count="virtualDisplayMaxCount"
      :virtual-display-pending="virtualDisplayPending"
      :request-virtual-display="requestVirtualDisplay"
      :disconnect="disconnect"
      :log="addLog"
    />

    <audio ref="voiceAudioRef" autoplay playsinline></audio>

    <div v-if="pointerLocked" class="lock-hint">{{ t('app.pointerLocked') }}</div>

    <!-- 右下角:统计 + 日志 -->
    <div v-if="perfVisible || logVisible" class="side-dock">
      <div v-if="perfVisible" class="perf-panel">
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.bitrate') }}</span>
          <span class="perf-value">{{ perfBitrateText }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.fps') }}</span>
          <span class="perf-value">{{ perf.fps.toFixed(0) }} fps</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.decode') }}</span>
          <span class="perf-value">{{ perf.decFps.toFixed(0) }} fps</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.drop') }}</span>
          <span class="perf-value">{{ perf.dropFps.toFixed(1) }}/s</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.decodeMs') }}</span>
          <span class="perf-value">{{ perf.decodeMs.toFixed(1) }} ms</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.procMs') }}</span>
          <span class="perf-value">{{ perf.procMs.toFixed(1) }} ms</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.decoder') }}</span>
          <span class="perf-value">{{ perf.decoder || '-' }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.freeze') }}</span>
          <span class="perf-value">{{
            t('perf.freezeValue', { count: perf.freezes, ms: perf.freezeMs.toFixed(0) })
          }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.jbTarget') }}</span>
          <span class="perf-value">{{ perf.jbTargetMs.toFixed(0) }} ms</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.jbActual') }}</span>
          <span class="perf-value">{{ perf.jbDelayMs.toFixed(0) }} ms</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.rtt') }}</span>
          <span class="perf-value">{{ perfRttText }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.inputRtt') }}</span>
          <span class="perf-value">{{ pingRttMs >= 0 ? pingRttMs.toFixed(1) + ' ms' : '-' }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.loss') }}</span>
          <span class="perf-value">{{ perfLossText }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.jitter') }}</span>
          <span class="perf-value">{{ perfJitterText }}</span>
        </div>
        <div class="perf-item">
          <span class="perf-label">{{ t('perf.resolution') }}</span>
          <span class="perf-value">{{ perfResolutionText }}</span>
        </div>
        <div class="perf-item perf-path">
          <span class="perf-label">{{ t('perf.path') }}</span>
          <span class="perf-value">{{ perf.localCand || '?' }} ↔ {{ perf.remoteCand || '?' }}</span>
        </div>
        <div class="perf-note">{{ t('perf.note') }}</div>
      </div>

      <div v-if="logVisible" class="log-panel">
        <div class="log-toolbar">
          <span class="log-title">{{ t('logPanel.title') }}</span>
          <span class="log-meta">{{ t('logPanel.lines', { n: logs.length }) }} · {{ t('logPanel.maxHint', { n: MAX_LOG_LINES }) }}</span>
          <div class="log-actions">
            <button type="button" @click="selectAllLogs">{{ t('logPanel.selectAll') }}</button>
            <button type="button" @click="deselectAllLogs">{{ t('logPanel.deselectAll') }}</button>
            <button type="button" @click="clearLogs">{{ t('logPanel.clear') }}</button>
          </div>
        </div>
        <div ref="logBodyRef" class="log-body">
          <div v-if="!logs.length" class="log-empty">{{ t('logPanel.empty') }}</div>
          <div v-for="(line, i) in logs" :key="i" class="log-line">{{ line }}</div>
        </div>
      </div>
    </div>

    <FileTransferWindow v-if="hasGrantedPermission('file')" v-model:visible="ftVisible" :device-id="form.deviceId" :ft="ft" />
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
.lock-hint {
  position: absolute;
  top: 16px;
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
.loading-page {
  position: absolute;
  inset: 0;
  z-index: 40;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  background: radial-gradient(ellipse at center, #1a2332 0%, #0b0f14 70%);
  color: #e8eef7;
}
.loading-logo {
  width: 72px;
  height: 72px;
  object-fit: contain;
  margin-bottom: 4px;
}
.loading-title {
  font-size: 20px;
  font-weight: 600;
  letter-spacing: 0.02em;
}
.loading-hint {
  font-size: 13px;
  color: #9aa7b8;
  max-width: 420px;
  text-align: center;
  padding: 0 16px;
}
.loading-hint.error {
  color: #f89898;
}
.loading-step-meta {
  font-size: 12px;
  color: #7f93ad;
  margin-top: 2px;
}
.loading-steps {
  list-style: none;
  margin: 8px 0 0;
  padding: 0;
  width: min(320px, calc(100vw - 48px));
  display: flex;
  flex-direction: column;
  gap: 6px;
  text-align: left;
}
.loading-step {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 13px;
  color: #6b7c90;
}
.loading-step.done {
  color: #8fca9a;
}
.loading-step.active {
  color: #e8eef7;
  font-weight: 600;
}
.loading-step.active .step-mark {
  border-color: #5b9cff;
  background: #5b9cff;
  box-shadow: 0 0 0 3px rgba(91, 156, 255, 0.25);
}
.loading-step.done .step-mark {
  border-color: #5cb86a;
  background: #5cb86a;
}
.step-mark {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  border: 2px solid #4a5a6e;
  flex-shrink: 0;
  background: transparent;
}
.loading-spinner {
  width: 28px;
  height: 28px;
  margin-top: 8px;
  border: 3px solid rgba(255, 255, 255, 0.15);
  border-top-color: #5b9cff;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}
.loading-action {
  margin-top: 10px;
  padding: 8px 22px;
  border: none;
  border-radius: 8px;
  background: #3d7eff;
  color: #fff;
  font-size: 14px;
  cursor: pointer;
}
.loading-action:hover {
  background: #5b9cff;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
.side-dock {
  position: absolute;
  right: 12px;
  bottom: 12px;
  z-index: 30;
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 8px;
  width: min(520px, calc(100vw - 24px));
  max-height: calc(100vh - 96px);
  pointer-events: auto;
}
.perf-panel {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px 10px;
  padding: 10px 12px;
  background: rgba(20, 24, 30, 0.92);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 10px;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
}
.perf-item {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  min-width: 0;
}
.perf-item.perf-path {
  grid-column: 1 / -1;
}
.perf-label {
  color: #8b95a5;
  font-size: 11px;
}
.perf-value {
  color: #7dffa0;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 12px;
  word-break: break-all;
}
.perf-note {
  grid-column: 1 / -1;
  color: #6a7380;
  font-size: 11px;
}
.log-panel {
  display: flex;
  flex-direction: column;
  min-height: 180px;
  max-height: min(42vh, 420px);
  background: rgba(12, 16, 22, 0.94);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 10px;
  overflow: hidden;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
}
.log-toolbar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  background: rgba(255, 255, 255, 0.03);
}
.log-title {
  color: #e8eef7;
  font-size: 13px;
  font-weight: 600;
}
.log-meta {
  color: #8b95a5;
  font-size: 11px;
  flex: 1;
}
.log-actions {
  display: flex;
  gap: 6px;
}
.log-actions button {
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.06);
  color: #d7dee8;
  border-radius: 5px;
  padding: 3px 8px;
  font-size: 12px;
  cursor: pointer;
}
.log-actions button:hover {
  background: rgba(255, 255, 255, 0.12);
}
.log-body {
  flex: 1;
  overflow: auto;
  padding: 8px 10px;
  user-select: text;
}
.log-empty {
  color: #6a7380;
  font-size: 12px;
  padding: 12px 0;
}
.log-line {
  color: #9fefb0;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 12px;
  line-height: 1.45;
  white-space: pre-wrap;
  word-break: break-all;
}
@media (max-width: 640px) {
  .perf-panel {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
