// WebRTC 卡顿诊断:采样 inbound/outbound RTP 与 candidate-pair 明细
// 重点看 qualityLimitationReason(bandwidth/cpu/other)、packetsLost、rtt、jitterBufferDelay
import { spawn, spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  process.env.WEB_URL ||
  `http://127.0.0.1:${process.env.RENDER_PORT || '32000'}/web_client/?deviceId=${encodeURIComponent(process.env.DEVICE_ID || 'debug1')}`
const CDP_PORT = Number(process.env.CDP_PORT || 9224)
const SAMPLE_SECONDS = Number(process.env.SAMPLE_SECONDS || 45)
const EXPECT_CANDIDATE_TYPE = process.env.EXPECT_CANDIDATE_TYPE || ''
const EXPECT_RELAY_PROTOCOL = process.env.EXPECT_RELAY_PROTOCOL || ''
const FORCE_RELAY = process.env.FORCE_RELAY === '1'
const QUIET = process.env.QUIET === '1'

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = process.env.CDP_PROFILE || path.join(os.tmpdir(), `cdp-diag-${Date.now()}`)
// Node's built-in WebSocket does not keep the event loop referenced while the
// DevTools handshake is pending. A fast-detaching Chrome parent could
// otherwise make this acceptance tool exit 0 before `main()` reaches a gate.
const processKeepalive = setInterval(() => {}, 1000)
let chromeExit = null
let chromeStderr = ''
const chrome = spawn(CHROME, [
  '--headless=new', '--disable-gpu', '--disable-gpu-compositing',
  '--disable-gpu-sandbox', '--no-sandbox', '--use-angle=swiftshader',
  '--disable-extensions', '--disable-background-networking',
  '--disable-component-update', '--disable-sync', '--no-proxy-server',
  `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`,
  '--no-first-run', 'about:blank',
], {
  stdio: ['ignore', 'ignore', 'pipe'],
})
chrome.stderr?.on('data', (chunk) => {
  if (chromeStderr.length < 4000) chromeStderr += String(chunk)
})
chrome.on('exit', (code, signal) => { chromeExit = { code, signal } })
let chromeCleaned = false
function cleanupChrome() {
  if (chromeCleaned) return
  chromeCleaned = true
  if (chrome.pid) {
    spawnSync('taskkill.exe', ['/PID', String(chrome.pid), '/T', '/F'], {
      stdio: 'ignore', windowsHide: true,
    })
  }
  try { chrome.kill() } catch { /* ignore */ }
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }) } catch { /* best effort */ }
}
process.on('exit', cleanupChrome)

let msgId = 0
const pending = new Map()
let ws
function cmd(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    const timeout = setTimeout(() => {
      pending.delete(id)
      reject(new Error(`DevTools command timeout: ${method}`))
    }, 10000)
    pending.set(id, {
      resolve: (value) => { clearTimeout(timeout); resolve(value) },
      reject: (error) => { clearTimeout(timeout); reject(error) },
    })
    try {
      ws.send(JSON.stringify({ id, method, params }))
    } catch (error) {
      clearTimeout(timeout)
      pending.delete(id)
      reject(error)
    }
  })
}
async function evaluate(expression) {
  const r = await cmd('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true })
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`, {
        signal: AbortSignal.timeout(500),
      })
      if (r.ok) return
    } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error(`devtools 未就绪; chrome=${JSON.stringify(chromeExit)} stderr=${chromeStderr.slice(-1000)}`)
}

const STATS_JS = `(async () => {
  const pc = window.__pc
  if (!pc) return { err: 'no __pc' }
  const stats = await pc.getStats()
  const out = { conn: pc.connectionState, ice: pc.iceConnectionState }
  const candidates = {}
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
    if (s.type === 'local-candidate' || s.type === 'remote-candidate') {
      candidates[s.id] = {
        ip: s.ip || s.address || '', port: s.port, type: s.candidateType,
        proto: s.protocol, relayProto: s.relayProtocol || '',
      }
    }
  })
  if (out.pair) {
    out.localCand = candidates[out.pair.localId] || null
    out.remoteCand = candidates[out.pair.remoteId] || null
  }
  return out
})()`

async function main() {
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent('about:blank')}`, {
    method: 'PUT', signal: AbortSignal.timeout(5000),
  })
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
  await Promise.race([
    new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej }),
    sleep(10000).then(() => { throw new Error('DevTools WebSocket 连接超时') }),
  ])
  await cmd('Runtime.enable')
  await cmd('Page.enable')
  if (FORCE_RELAY) {
    await cmd('Page.addScriptToEvaluateOnNewDocument', {
      source: `(() => {
        const NativePeerConnection = window.RTCPeerConnection
        window.RTCPeerConnection = new Proxy(NativePeerConnection, {
          construct(target, args) {
            args[0] = { ...(args[0] || {}), iceTransportPolicy: 'relay' }
            return Reflect.construct(target, args)
          }
        })
      })()`,
    })
  }
  await cmd('Page.navigate', { url: PAGE_URL })

  let connected = false
  for (let i = 0; i < 60; i++) {
    const st = await evaluate(`window.__pc ? window.__pc.connectionState : 'none'`).catch(() => 'err')
    if (st === 'connected') { connected = true; break }
    await sleep(500)
  }
  if (!QUIET) console.log('connected:', connected)
  if (!connected) throw new Error('未连接')

  let prev = null
  let first = null
  let last = null
  let lastWithPair = null
  let pairSamples = 0
  let missingPairSamples = 0
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
    if (!QUIET) console.log(`[t=${t + 3}s]`, JSON.stringify(s))
    if (!first?.inbound && s.inbound) first = s
    if (s.inbound) last = s
    if (s.pair && s.localCand && s.remoteCand) {
      lastWithPair = s
      pairSamples += 1
    } else {
      missingPairSamples += 1
    }
    prev = s
  }
  if (!first?.inbound || !last?.inbound) throw new Error('观测窗口缺少视频统计')
  if (!lastWithPair?.pair) throw new Error('观测窗口从未出现 selected candidate 统计')
  const frameDelta = last.inbound.dec - first.inbound.dec
  if (frameDelta < Math.max(10, SAMPLE_SECONDS * 5)) {
    throw new Error(`视频解码帧增长不足: ${frameDelta}`)
  }
  const observedLost = (last.inbound.lost || 0) - (first.inbound.lost || 0)
  const observedFreezes = (last.inbound.freezeCount || 0) - (first.inbound.freezeCount || 0)
  if (observedLost > 0 || observedFreezes > 0) {
    throw new Error(`LAN 稳态观测窗口存在新增丢包或冻结: lostDelta=${observedLost} freezeDelta=${observedFreezes}`)
  }
  const selected = [lastWithPair.localCand, lastWithPair.remoteCand].filter(Boolean)
  if (EXPECT_CANDIDATE_TYPE && !selected.some((candidate) => candidate.type === EXPECT_CANDIDATE_TYPE)) {
    throw new Error(`selected candidate 不是 ${EXPECT_CANDIDATE_TYPE}: ${JSON.stringify(selected)}`)
  }
  if (EXPECT_RELAY_PROTOCOL && !selected.some((candidate) =>
    candidate.type === 'relay' && candidate.relayProto === EXPECT_RELAY_PROTOCOL)) {
    throw new Error(`selected TURN transport 不是 ${EXPECT_RELAY_PROTOCOL}: ${JSON.stringify(selected)}`)
  }
  console.log('RESULT: PASS', JSON.stringify({
    frameDelta,
    startupLost: first.inbound.lost || 0,
    observedLost,
    observedFreezes,
    pairSamples,
    missingPairSamples,
    resolution: `${last.inbound.frameWidth}x${last.inbound.frameHeight}`,
    rttMs: (lastWithPair.pair.rtt || 0) * 1000,
    localCandidate: lastWithPair.localCand,
    remoteCandidate: lastWithPair.remoteCand,
  }))
}

main()
  .catch((e) => {
    console.error('异常:', e.message)
    console.error('Chrome:', JSON.stringify(chromeExit), chromeStderr.slice(-1000))
    process.exitCode = 1
  })
  .finally(() => {
    clearInterval(processKeepalive)
    try { ws?.close() } catch { /* ignore */ }
    cleanupChrome()
    setTimeout(() => process.exit(process.exitCode ?? 0), 500).unref()
  })
