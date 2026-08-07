// WebRTC 卡顿诊断:采样 inbound/outbound RTP 与 candidate-pair 明细
// 重点看 qualityLimitationReason(bandwidth/cpu/other)、packetsLost、rtt、jitterBufferDelay
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  process.env.WEB_URL ||
  `http://127.0.0.1:${process.env.RENDER_PORT || '32000'}/web_client/?deviceId=${encodeURIComponent(process.env.DEVICE_ID || 'debug1')}`
const CDP_PORT = Number(process.env.CDP_PORT || 9224)
const SAMPLE_SECONDS = Number(process.env.SAMPLE_SECONDS || 45)

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-diag-${Date.now()}`)
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`, '--no-first-run', 'about:blank'], { stdio: 'ignore' })
process.on('exit', () => { try { chrome.kill() } catch { /* ignore */ } })

let msgId = 0
const pending = new Map()
let ws
function cmd(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    pending.set(id, { resolve, reject })
    ws.send(JSON.stringify({ id, method, params }))
  })
}
async function evaluate(expression) {
  const r = await cmd('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true })
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) return } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools 未就绪')
}

const STATS_JS = `(async () => {
  const pc = window.__pc
  if (!pc) return { err: 'no __pc' }
  const stats = await pc.getStats()
  const out = { conn: pc.connectionState, ice: pc.iceConnectionState }
  stats.forEach((s) => {
    if (s.type === 'inbound-rtp' && s.kind === 'video') {
      out.inbound = {
        fps: s.framesPerSecond, recv: s.framesReceived, dec: s.framesDecoded,
        lost: s.packetsLost, pkts: s.packetsReceived, jitter: s.jitter,
        jbDelay: s.jitterBufferDelay, jbTarget: s.jitterBufferTarget,
        keyDec: s.keyFramesDecoded, freezeCount: s.freezeCount, totalFreezesDuration: s.totalFreezesDuration,
        bytes: s.bytesReceived, frameWidth: s.frameWidth, frameHeight: s.frameHeight,
        decoder: s.decoderImplementation,
      }
    }
    if (s.type === 'outbound-rtp' && s.kind === 'video') {
      out.outbound = { fps: s.framesPerSecond, bytes: s.bytesSent, reason: s.qualityLimitationReason, target: s.targetBitrate }
    }
    if (s.type === 'remote-outbound-rtp' && s.kind === 'video') {
      out.remoteOut = { bytes: s.bytesSent, pkts: s.packetsSent, ts: Math.round(s.remoteTimestamp ?? 0) }
    }
    if (s.type === 'candidate-pair' && s.state === 'succeeded' && s.nominated) {
      out.pair = {
        rtt: s.currentRoundTripTime, outBwe: s.availableOutgoingBitrate, inBwe: s.availableIncomingBitrate,
        localId: s.localCandidateId, remoteId: s.remoteCandidateId,
        sentBytes: s.bytesSent, recvBytes: s.bytesReceived,
      }
    }
    if (s.type === 'local-candidate') out.localCand = { ip: s.ip, port: s.port, type: s.candidateType, proto: s.protocol }
    if (s.type === 'remote-candidate') out.remoteCand = { ip: s.ip, port: s.port, type: s.candidateType, proto: s.protocol }
  })
  return out
})()`

async function main() {
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, { method: 'PUT' })
  const target = await r.json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data)
    if (m.id && pending.has(m.id)) {
      const p = pending.get(m.id)
      pending.delete(m.id)
      m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result)
    }
  }
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej })
  await cmd('Runtime.enable')

  let connected = false
  for (let i = 0; i < 60; i++) {
    const st = await evaluate(`window.__pc ? window.__pc.connectionState : 'none'`).catch(() => 'err')
    if (st === 'connected') { connected = true; break }
    await sleep(500)
  }
  console.log('connected:', connected)
  if (!connected) throw new Error('未连接')

  let prev = null
  for (let t = 0; t < SAMPLE_SECONDS; t += 3) {
    await sleep(3000)
    const s = await evaluate(STATS_JS).catch((e) => ({ err: e.message }))
    if (s.inbound && prev?.inbound) {
      const dt = 3
      const kbps = Math.round(((s.inbound.bytes - prev.inbound.bytes) * 8) / dt / 1000)
      const lostDelta = s.inbound.lost - prev.inbound.lost
      const pktsDelta = s.inbound.pkts - prev.inbound.pkts
      s.calc = {
        kbps,
        recvFps: Math.round(((s.inbound.recv - prev.inbound.recv) / dt) * 10) / 10,
        decFps: Math.round(((s.inbound.dec - prev.inbound.dec) / dt) * 10) / 10,
        lostRate: pktsDelta > 0 ? Math.round((lostDelta / pktsDelta) * 1000) / 10 : 0,
      }
    }
    console.log(`[t=${t + 3}s]`, JSON.stringify(s))
    prev = s
  }
}

main()
  .catch((e) => { console.error('异常:', e.message); process.exitCode = 1 })
  .finally(() => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    setTimeout(() => process.exit(process.exitCode ?? 0), 500).unref()
  })
