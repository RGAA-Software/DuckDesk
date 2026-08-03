// WebRTC 性能采样:每 2s 读 pc.getStats(),产出视频码率/帧率/RTT/丢包/抖动/分辨率
// 码率为采样间隔内 inbound-rtp bytesReceived 差值推算(WebRTC 自适应码率,
// 协议无改码率消息,tc_message.proto 仅有 kModifyFps,故只做展示)

export interface PerfStats {
  // 视频接收码率(kbps),首个采样周期无上一帧基准时为 0
  videoBitrateKbps: number
  // 解码帧率( inbound-rtp framesPerSecond,可能缺失则为 0)
  fps: number
  // 候选对 RTT(ms)
  rttMs: number
  // 采样间隔内丢包率(0~1)
  lossRate: number
  // 抖动(ms)
  jitterMs: number
  // 当前接收分辨率
  width: number
  height: number
}

export const EMPTY_PERF: PerfStats = {
  videoBitrateKbps: 0,
  fps: 0,
  rttMs: 0,
  lossRate: 0,
  jitterMs: 0,
  width: 0,
  height: 0,
}

const SAMPLE_INTERVAL_MS = 2000

export class PerfCollector {
  private pc: RTCPeerConnection | null = null
  private timer: number | null = null
  private lastBytes = -1
  private lastLost = -1
  private lastReceived = -1
  private lastAt = 0

  constructor(private onUpdate: (s: PerfStats) => void) {}

  start(pc: RTCPeerConnection) {
    this.stop()
    this.pc = pc
    this.lastBytes = -1
    this.lastLost = -1
    this.lastReceived = -1
    this.lastAt = 0
    void this.sample()
    this.timer = window.setInterval(() => void this.sample(), SAMPLE_INTERVAL_MS)
  }

  stop() {
    if (this.timer !== null) {
      window.clearInterval(this.timer)
      this.timer = null
    }
    this.pc = null
  }

  private async sample() {
    const pc = this.pc
    if (!pc || pc.connectionState !== 'connected') return
    let stats: RTCStatsReport
    try {
      stats = await pc.getStats()
    } catch {
      return
    }
    // 采样期间连接被断开
    if (this.pc !== pc) return

    const out: PerfStats = { ...EMPTY_PERF }
    let bytes = -1
    let lost = -1
    let received = -1
    stats.forEach((report) => {
      if (report.type === 'inbound-rtp') {
        const r = report as RTCInboundRtpStreamStats & {
          framesPerSecond?: number
          frameWidth?: number
          frameHeight?: number
        }
        if (r.kind !== 'video') return
        bytes = r.bytesReceived ?? 0
        lost = r.packetsLost ?? 0
        received = r.packetsReceived ?? 0
        out.fps = r.framesPerSecond ?? 0
        out.jitterMs = (r.jitter ?? 0) * 1000
        out.width = r.frameWidth ?? 0
        out.height = r.frameHeight ?? 0
      } else if (report.type === 'candidate-pair') {
        const p = report as RTCIceCandidatePairStats
        if (p.nominated && p.currentRoundTripTime !== undefined) {
          out.rttMs = p.currentRoundTripTime * 1000
        }
      }
    })

    const now = Date.now()
    if (bytes >= 0 && this.lastBytes >= 0 && now > this.lastAt) {
      const dtSec = (now - this.lastAt) / 1000
      out.videoBitrateKbps = Math.max(0, ((bytes - this.lastBytes) * 8) / dtSec / 1000)
      const dLost = lost - this.lastLost
      const dRecv = received - this.lastReceived
      out.lossRate = dLost + dRecv > 0 ? Math.max(0, dLost) / (dLost + dRecv) : 0
    }
    if (bytes >= 0) {
      this.lastBytes = bytes
      this.lastLost = lost
      this.lastReceived = received
      this.lastAt = now
    }
    this.onUpdate(out)
  }
}
