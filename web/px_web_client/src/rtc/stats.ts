// WebRTC 性能采样:每 2s 读 pc.getStats(),产出视频码率/帧率/RTT/丢包/抖动/分辨率,
// 以及延迟/卡顿诊断关键指标:
//   jbTargetMs  — 采样窗口内抖动缓冲目标均摊(ΔtargetDelay/Δemitted,ms);勿用 lifetime 均值
//   jbDelayMs   — 采样窗口内抖动缓冲实际均摊(Δdelay/Δemitted,ms)
//   decodeMs    — 每帧解码耗时(软解 4:4:4 时会飙到几十 ms,直接拖垮 fps)
//   procMs      — 每帧总处理耗时(解码+渲染排队,Chrome 收端处理不过来的直接证据)
//   decoder     — 解码器实现(ExternalDecoder≈硬解;FFmpegDecoder=软解)
//   dropFps     — 接收端丢帧速率(解码/渲染跟不上时增长)
//   路径        — 选中 candidate-pair 的 local/remote 类型与地址(host+host 为直连)

export interface PerfStats {
  // 视频接收码率(kbps),首个采样周期无上一帧基准时为 0
  videoBitrateKbps: number
  // 接收帧率(inbound-rtp framesPerSecond,可能缺失则为 0)
  fps: number
  // 解码帧率(framesDecoded 差值/间隔)
  decFps: number
  // 接收端丢帧速率(framesDropped 差值/间隔)
  dropFps: number
  // 抖动缓冲目标均摊延迟(ms/帧,与 webrtc-internals 一致)
  jbTargetMs: number
  // 抖动缓冲实际均摊延迟(ms/帧)
  jbDelayMs: number
  // 每帧解码耗时(ms)
  decodeMs: number
  // 每帧总处理耗时(解码+渲染,ms)
  procMs: number
  // 解码器实现(ExternalDecoder=硬解 / FFmpegDecoder=软解 / 空=未知)
  decoder: string
  // 候选对 RTT(ms)
  rttMs: number
  // 采样间隔内丢包率(0~1)
  lossRate: number
  // 抖动(ms)
  jitterMs: number
  // 当前接收分辨率
  width: number
  height: number
  // 采样间隔内解码的关键帧数(频繁>0 说明对端在 IDR 风暴/断链重建)
  keyDecoded: number
  // 采样间隔内冻结次数与冻结总时长(ms)——卡顿体感的最直接指标
  freezes: number
  freezeMs: number
  // 选中 candidate-pair 的两端描述,如 "host 10.0.0.16:52341/udp"
  localCand: string
  remoteCand: string
}

export const EMPTY_PERF: PerfStats = {
  videoBitrateKbps: 0,
  fps: 0,
  decFps: 0,
  dropFps: 0,
  jbTargetMs: 0,
  jbDelayMs: 0,
  decodeMs: 0,
  procMs: 0,
  decoder: '',
  rttMs: 0,
  lossRate: 0,
  jitterMs: 0,
  width: 0,
  height: 0,
  keyDecoded: 0,
  freezes: 0,
  freezeMs: 0,
  localCand: '',
  remoteCand: '',
}

// 一行紧凑摘要,用于周期性写入日志面板(用户可直接复制发回)
export function perfSummaryLine(s: PerfStats): string {
  const res = s.width > 0 ? `${s.width}x${s.height}` : '-'
  return (
    `[perf] recv=${s.fps.toFixed(0)}fps dec=${s.decFps.toFixed(0)}fps drop=${s.dropFps.toFixed(1)}fps ` +
    `jbTarget=${s.jbTargetMs.toFixed(0)}ms jbDelay=${s.jbDelayMs.toFixed(0)}ms ` +
    `decode=${s.decodeMs.toFixed(1)}ms proc=${s.procMs.toFixed(1)}ms dec_impl=${s.decoder || '-'} ` +
    `keys=${s.keyDecoded} freezes=${s.freezes}(${s.freezeMs.toFixed(0)}ms) ` +
    `br=${(s.videoBitrateKbps / 1000).toFixed(1)}Mbps rtt=${s.rttMs.toFixed(1)}ms ` +
    `loss=${(s.lossRate * 100).toFixed(1)}% jitter=${s.jitterMs.toFixed(1)}ms res=${res} ` +
    `path=${s.localCand || '?'} <-> ${s.remoteCand || '?'}`
  )
}

const SAMPLE_INTERVAL_MS = 2000

// getStats 各 report 的松散类型(标准字段 + Chrome 扩展字段)
type LooseStats = {
  id?: string
  kind?: string
  bytesReceived?: number
  packetsLost?: number
  packetsReceived?: number
  framesPerSecond?: number
  framesReceived?: number
  framesDecoded?: number
  framesDropped?: number
  frameWidth?: number
  frameHeight?: number
  jitter?: number
  jitterBufferDelay?: number
  jitterBufferTargetDelay?: number
  jitterBufferEmittedCount?: number
  totalDecodeTime?: number
  totalProcessingDelay?: number
  decoderImplementation?: string
  keyFramesDecoded?: number
  freezeCount?: number
  totalFreezesDuration?: number
  nominated?: boolean
  currentRoundTripTime?: number
  localCandidateId?: string
  remoteCandidateId?: string
  ip?: string
  address?: string // 新版 Chrome 候选者用 address 替代 ip
  port?: number
  candidateType?: string
  protocol?: string
  // Chrome exposes the transport used between the endpoint and TURN here.
  // A TURN/TCP allocation still relays UDP media, so `protocol` alone would
  // misleadingly report `/udp` and hide the TCP fallback from diagnostics.
  relayProtocol?: string
}

export class PerfCollector {
  private pc: RTCPeerConnection | null = null
  private timer: number | null = null
  private lastBytes = -1
  private lastLost = -1
  private lastReceived = -1
  private lastDecoded = -1
  private lastDropped = -1
  private lastDecodeTime = -1
  private lastProcDelay = -1
  private lastJbDelay = -1
  private lastJbTarget = -1
  private lastJbEmitted = -1
  private lastKeyDecoded = -1
  private lastFreezes = -1
  private lastFreezeDur = -1
  private lastAt = 0

  constructor(private onUpdate: (s: PerfStats) => void) {}

  start(pc: RTCPeerConnection) {
    this.stop()
    this.pc = pc
    this.lastBytes = -1
    this.lastLost = -1
    this.lastReceived = -1
    this.lastDecoded = -1
    this.lastDropped = -1
    this.lastDecodeTime = -1
    this.lastProcDelay = -1
    this.lastJbDelay = -1
    this.lastJbTarget = -1
    this.lastJbEmitted = -1
    this.lastKeyDecoded = -1
    this.lastFreezes = -1
    this.lastFreezeDur = -1
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
    let decoded = -1
    let dropped = -1
    let decodeTime = -1
    let procDelay = -1
    let keyDecoded = -1
    let freezes = -1
    let freezeDur = -1
    let jbDelay = -1
    let jbTarget = -1
    let jbEmitted = -1

    // candidate-pair 引用 local/remote candidate 的 id,先收集候选表再回填
    const candidates = new Map<string, LooseStats>()
    let pairLocalId = ''
    let pairRemoteId = ''

    stats.forEach((report) => {
      const r = report as unknown as LooseStats & { type: string }
      if (r.type === 'inbound-rtp' && r.kind === 'video') {
        bytes = r.bytesReceived ?? 0
        lost = r.packetsLost ?? 0
        received = r.packetsReceived ?? 0
        decoded = r.framesDecoded ?? 0
        dropped = r.framesDropped ?? 0
        decodeTime = r.totalDecodeTime ?? 0
        procDelay = r.totalProcessingDelay ?? 0
        keyDecoded = r.keyFramesDecoded ?? 0
        freezes = r.freezeCount ?? 0
        freezeDur = r.totalFreezesDuration ?? 0
        jbDelay = r.jitterBufferDelay ?? 0
        jbTarget = r.jitterBufferTargetDelay ?? 0
        jbEmitted = r.jitterBufferEmittedCount ?? 0
        out.fps = r.framesPerSecond ?? 0
        out.jitterMs = (r.jitter ?? 0) * 1000
        out.width = r.frameWidth ?? 0
        out.height = r.frameHeight ?? 0
        out.decoder = r.decoderImplementation ?? ''
      } else if (r.type === 'candidate-pair' && r.nominated) {
        out.rttMs = (r.currentRoundTripTime ?? 0) * 1000
        pairLocalId = r.localCandidateId ?? ''
        pairRemoteId = r.remoteCandidateId ?? ''
      } else if (r.type === 'local-candidate' || r.type === 'remote-candidate') {
        if (r.id) candidates.set(r.id, r)
      }
    })

    const fmtCand = (c?: LooseStats): string => {
      if (!c) return '?'
      const ip = c.ip ?? c.address ?? '?'
      const turnTransport = c.candidateType === 'relay' && c.relayProtocol
        ? ` via turn:${c.relayProtocol}`
        : ''
      return `${c.candidateType ?? '?'} ${ip}:${c.port ?? '?'}/${c.protocol ?? '?'}${turnTransport}`
    }
    out.localCand = fmtCand(candidates.get(pairLocalId))
    out.remoteCand = fmtCand(candidates.get(pairRemoteId))

    const now = Date.now()
    if (bytes >= 0 && this.lastBytes >= 0 && now > this.lastAt) {
      const dtSec = (now - this.lastAt) / 1000
      out.videoBitrateKbps = Math.max(0, ((bytes - this.lastBytes) * 8) / dtSec / 1000)
      const dLost = lost - this.lastLost
      const dRecv = received - this.lastReceived
      out.lossRate = dLost + dRecv > 0 ? Math.max(0, dLost) / (dLost + dRecv) : 0
      out.decFps = Math.max(0, (decoded - this.lastDecoded) / dtSec)
      out.dropFps = Math.max(0, (dropped - this.lastDropped) / dtSec)
      out.keyDecoded = Math.max(0, keyDecoded - this.lastKeyDecoded)
      out.freezes = Math.max(0, freezes - this.lastFreezes)
      out.freezeMs = Math.max(0, (freezeDur - this.lastFreezeDur) * 1000)
      const dDecoded = decoded - this.lastDecoded
      if (dDecoded > 0) {
        out.decodeMs = Math.max(0, ((decodeTime - this.lastDecodeTime) / dDecoded) * 1000)
        out.procMs = Math.max(0, ((procDelay - this.lastProcDelay) / dDecoded) * 1000)
      }
      // JB 累计秒/累计帧 = lifetime 均值:会话早期曾 ~1s 时,UI 会长期显示偏高。
      // 与 decode/proc 一样用采样窗口增量,才反映当前播放余量。
      const dEmitted = jbEmitted - this.lastJbEmitted
      if (dEmitted > 0 && this.lastJbEmitted >= 0) {
        out.jbDelayMs = Math.max(0, ((jbDelay - this.lastJbDelay) / dEmitted) * 1000)
        out.jbTargetMs = Math.max(0, ((jbTarget - this.lastJbTarget) / dEmitted) * 1000)
      }
    }
    if (bytes >= 0) {
      this.lastBytes = bytes
      this.lastLost = lost
      this.lastReceived = received
      this.lastDecoded = decoded
      this.lastDropped = dropped
      this.lastDecodeTime = decodeTime
      this.lastProcDelay = procDelay
      this.lastJbDelay = jbDelay
      this.lastJbTarget = jbTarget
      this.lastJbEmitted = jbEmitted
      this.lastKeyDecoded = keyDecoded
      this.lastFreezes = freezes
      this.lastFreezeDur = freezeDur
      this.lastAt = now
    }
    this.onUpdate(out)
  }
}
