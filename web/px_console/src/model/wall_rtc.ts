export type WallRtcState = 'connecting' | 'connected' | 'playing' | 'error' | 'closed'

export interface WallRtcStats {
  width: number
  height: number
  fps: number
  bitrateKbps: number
  rttMs: number
  codec: string
}

interface WallSignalData {
  session_id: string
  answer_sdp: string
  render_ip: string
  render_port: number
}

interface WallRtcCallbacks {
  onState: (state: WallRtcState, message?: string) => void
  onStream: (stream: MediaStream) => void
  onStats: (stats: WallRtcStats) => void
  onEndpoint: (ip: string, port: number) => void
}

const waitForIceGathering = (pc: RTCPeerConnection, timeoutMs = 6000) =>
  new Promise<void>((resolve) => {
    if (pc.iceGatheringState === 'complete') {
      resolve()
      return
    }
    const finish = () => {
      window.clearTimeout(timer)
      pc.removeEventListener('icegatheringstatechange', changed)
      resolve()
    }
    const changed = () => {
      if (pc.iceGatheringState === 'complete') finish()
    }
    const timer = window.setTimeout(finish, timeoutMs)
    pc.addEventListener('icegatheringstatechange', changed)
  })

export class WallRtcSession {
  private pc: RTCPeerConnection | null = null
  private statsTimer = 0
  private closed = false
  private lastBytes = 0
  private lastStatsAt = 0
  private hasVideoTrack = false
  private hasDecodedFrame = false

  constructor(
    private readonly deviceId: string,
    private readonly callbacks: WallRtcCallbacks,
  ) {}

  async start() {
    this.close(false)
    this.closed = false
    this.callbacks.onState('connecting')
    const pc = new RTCPeerConnection({
      iceServers: [],
      bundlePolicy: 'max-bundle',
      rtcpMuxPolicy: 'require',
    })
    this.pc = pc

    const transceiver = pc.addTransceiver('video', { direction: 'recvonly' })
    const caps = RTCRtpReceiver.getCapabilities('video')
    if (caps && 'setCodecPreferences' in transceiver) {
      const h264 = caps.codecs.filter((codec) => codec.mimeType.toLowerCase() === 'video/h264')
      const rest = caps.codecs.filter((codec) => codec.mimeType.toLowerCase() !== 'video/h264')
      if (h264.length > 0) transceiver.setCodecPreferences([...h264, ...rest])
    }

    pc.ontrack = (event) => {
      if (event.track.kind !== 'video') return
      this.hasVideoTrack = true
      this.callbacks.onStream(event.streams[0] ?? new MediaStream([event.track]))
      this.callbacks.onState('connected')
    }
    pc.onconnectionstatechange = () => {
      if (this.closed) return
      if (pc.connectionState === 'connected') {
        this.callbacks.onState(this.hasDecodedFrame ? 'playing' : 'connected')
        this.startStats()
      } else if (pc.connectionState === 'disconnected') {
        // 短暂网络抖动时浏览器仍会自动恢复，不立即销毁会话。
        this.callbacks.onState('connecting', '媒体连接暂时中断，正在恢复…')
      } else if (pc.connectionState === 'failed') {
        this.callbacks.onState('error', 'WebRTC 连接失败')
      } else if (pc.connectionState === 'closed') {
        this.callbacks.onState('closed')
      }
    }

    try {
      const offer = await pc.createOffer({ offerToReceiveAudio: false, offerToReceiveVideo: true })
      await pc.setLocalDescription(offer)
      await waitForIceGathering(pc)
      if (this.closed || !pc.localDescription?.sdp) return

      const controller = new AbortController()
      const timeout = window.setTimeout(() => controller.abort(), 18000)
      const csrf = sessionStorage.getItem('px_admin_csrf') || ''
      const response = await fetch('/api/v1/wall/control/session', {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
        body: JSON.stringify({ device_id: this.deviceId, offer_sdp: pc.localDescription.sdp }),
        signal: controller.signal,
      }).finally(() => window.clearTimeout(timeout))
      const body = (await response.json()) as {
        code?: number
        message?: string
        data?: WallSignalData
      }
      if (!response.ok || body.code !== 200 || !body.data?.answer_sdp) {
        throw new Error(body.message || `信令失败 (HTTP ${response.status})`)
      }
      if (this.closed) return
      this.callbacks.onEndpoint(body.data.render_ip, body.data.render_port)
      await pc.setRemoteDescription({ type: 'answer', sdp: body.data.answer_sdp })
    } catch (error) {
      if (this.closed) return
      const message = error instanceof Error ? error.message : String(error)
      this.callbacks.onState('error', message)
      this.close(false)
    }
  }

  close(notify = true) {
    this.closed = true
    if (this.statsTimer) window.clearInterval(this.statsTimer)
    this.statsTimer = 0
    if (this.pc) {
      this.pc.ontrack = null
      this.pc.onconnectionstatechange = null
      this.pc.close()
      this.pc = null
    }
    this.lastBytes = 0
    this.lastStatsAt = 0
    this.hasVideoTrack = false
    this.hasDecodedFrame = false
    if (notify) this.callbacks.onState('closed')
  }

  private startStats() {
    if (this.statsTimer) return
    void this.collectStats()
    this.statsTimer = window.setInterval(() => void this.collectStats(), 1000)
  }

  private async collectStats() {
    if (!this.pc || this.closed) return
    const report = await this.pc.getStats()
    let inbound: RTCInboundRtpStreamStats | undefined
    let rttMs = 0
    report.forEach((stat) => {
      if (stat.type === 'inbound-rtp' && stat.kind === 'video') inbound = stat
      if (stat.type === 'candidate-pair' && stat.state === 'succeeded' && stat.currentRoundTripTime) {
        rttMs = Math.round(stat.currentRoundTripTime * 1000)
      }
    })
    if (!inbound) return
    const decodedFrames = inbound.framesDecoded ?? 0
    if (!this.hasDecodedFrame && decodedFrames > 0) {
      this.hasDecodedFrame = true
      this.callbacks.onState('playing')
    }
    const codecReport = inbound.codecId ? report.get(inbound.codecId) : undefined
    const codec = codecReport?.mimeType ? String(codecReport.mimeType).replace(/^video\//i, '') : ''
    const now = performance.now()
    const bytes = inbound.bytesReceived ?? 0
    const seconds = this.lastStatsAt ? (now - this.lastStatsAt) / 1000 : 0
    const bitrateKbps = seconds > 0 ? Math.max(0, Math.round(((bytes - this.lastBytes) * 8) / seconds / 1000)) : 0
    this.lastBytes = bytes
    this.lastStatsAt = now
    this.callbacks.onStats({
      width: inbound.frameWidth ?? 0,
      height: inbound.frameHeight ?? 0,
      fps: Math.round(inbound.framesPerSecond ?? 0),
      bitrateKbps,
      rttMs,
      codec,
    })
  }
}
