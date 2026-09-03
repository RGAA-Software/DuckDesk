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
const CONNECT_TIMEOUT_SECONDS = Number(process.env.CONNECT_TIMEOUT_SECONDS || 30)
const EXPECT_CANDIDATE_TYPE = process.env.EXPECT_CANDIDATE_TYPE || ''
const EXPECT_RELAY_PROTOCOL = process.env.EXPECT_RELAY_PROTOCOL || ''
const FORCE_RELAY = process.env.FORCE_RELAY === '1'
const TAKEOVER_CONFIRMATION = process.env.TAKEOVER_CONFIRMATION || 'default'
const QUIET = process.env.QUIET === '1'
const FT_E2E_BYTES = Number(process.env.FT_E2E_BYTES || 0)
const FT_CANCEL_E2E_BYTES = Number(process.env.FT_CANCEL_E2E_BYTES || 0)
const FT_TARGET_DIR = process.env.FT_TARGET_DIR || 'C:\\Windows\\Temp'
const FT_TIMEOUT_MS = Number(process.env.FT_TIMEOUT_MS || 300000)

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
  // LAN acceptance uses the bundled development Console certificate for the
  // cross-origin ticket-renewal endpoint. Production must use a trusted cert;
  // this keeps the browser harness from turning that local fixture into a
  // false RTC/takeover failure.
  '--ignore-certificate-errors',
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
const rtcSignalHttpStatuses = []
const rtcSignalResponseCodes = []
let ws
function cmd(method, params = {}, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    const timeout = setTimeout(() => {
      pending.delete(id)
      reject(new Error(`DevTools command timeout: ${method}`))
    }, timeoutMs)
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
async function evaluate(expression, timeoutMs = 10000) {
  const r = await cmd('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true }, timeoutMs)
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}

async function waitFtJob(jobId, label) {
  const deadline = Date.now() + FT_TIMEOUT_MS
  let last = null
  while (Date.now() < deadline) {
    const remaining = Math.max(1, deadline - Date.now())
    last = await evaluate(
      `window.__ft?.jobs?.().find((job) => job.id === ${Number(jobId)}) ?? null`,
      Math.min(2000, remaining),
    )
    if (last?.state === 'done') return last
    if (last?.state === 'error' || last?.state === 'cancelled') {
      throw new Error(`${label} failed: ${JSON.stringify(last)}`)
    }
    await sleep(250)
  }
  throw new Error(`${label} timed out: ${JSON.stringify(last)}`)
}

async function waitFtReady() {
  const readyDeadline = Date.now() + 30000
  while (Date.now() < readyDeadline) {
    const remaining = Math.max(1, readyDeadline - Date.now())
    if (await evaluate(
      `window.__ft?.ready?.() === true`, Math.min(1000, remaining),
    ).catch(() => false)) return
    await sleep(250)
  }
  throw new Error('file-transfer data channel did not become ready')
}

async function waitFtCancelled(jobId) {
  const deadline = Date.now() + FT_TIMEOUT_MS
  let last = null
  while (Date.now() < deadline) {
    last = await evaluate(
      `window.__ft?.jobs?.().find((job) => job.id === ${Number(jobId)}) ?? null`,
      2000,
    )
    if (last?.state === 'cancelled') return last
    if (last?.state === 'done' || last?.state === 'error') {
      throw new Error(`cancel reached unexpected terminal state: ${JSON.stringify(last)}`)
    }
    await sleep(100)
  }
  throw new Error(`cancel timed out: ${JSON.stringify(last)}`)
}

async function runFileTransferE2E() {
  if (!Number.isSafeInteger(FT_E2E_BYTES) || FT_E2E_BYTES <= 0) return null
  console.log(`FT_PHASE: waiting-ready bytes=${FT_E2E_BYTES}`)
  await waitFtReady()

  const name = `px_ft_backpressure_${Date.now()}.bin`
  const targetPath = `${FT_TARGET_DIR}\\${name}`
  console.log(`FT_PHASE: upload-start path=${targetPath}`)
  const uploaded = await evaluate(`(async () => {
    const size = ${FT_E2E_BYTES}
    let state = 0x6d2b79f5
    const chunks = []
    for (let offset = 0; offset < size; offset += 65536) {
      const bytes = new Uint8Array(Math.min(65536, size - offset))
      for (let index = 0; index < bytes.length; index += 1) {
        state ^= state << 13
        state ^= state >>> 17
        state ^= state << 5
        bytes[index] = 32 + ((state >>> 0) % 95)
      }
      chunks.push(new TextDecoder('ascii').decode(bytes))
    }
    const content = chunks.join('')
    return window.__ft.uploadText(${JSON.stringify(name)}, ${JSON.stringify(FT_TARGET_DIR)}, content)
  })()`, FT_TIMEOUT_MS)
  const uploadJob = await waitFtJob(uploaded.jobId, 'upload')
  console.log(`FT_PHASE: upload-done transferred=${uploadJob.transferred}`)
  console.log(`FT_PHASE: download-start path=${targetPath}`)
  const downloaded = await evaluate(
    `window.__ft.download(${JSON.stringify(targetPath)})`, FT_TIMEOUT_MS)
  if (downloaded.size !== uploaded.size || downloaded.sha256 !== uploaded.sha256) {
    throw new Error(`file hash mismatch: upload=${JSON.stringify(uploaded)} download=${JSON.stringify(downloaded)}`)
  }
  console.log(`FT_PHASE: download-done bytes=${downloaded.size}`)
  await evaluate(`window.__ft.removeFile(${JSON.stringify(targetPath)})`, 30000)
  const result = {
    bytes: uploaded.size,
    sha256: uploaded.sha256,
    uploadTransferred: uploadJob.transferred,
    remotePath: targetPath,
  }
  console.log('FT_E2E: PASS', JSON.stringify(result))
  return result
}

async function runFileTransferCancelE2E() {
  if (!Number.isSafeInteger(FT_CANCEL_E2E_BYTES) || FT_CANCEL_E2E_BYTES <= 0) return null
  await waitFtReady()
  const name = `px_ft_cancel_${Date.now()}.bin`
  const targetPath = `${FT_TARGET_DIR}\\${name}`
  console.log(`FT_CANCEL_PHASE: upload-start bytes=${FT_CANCEL_E2E_BYTES} path=${targetPath}`)
  const started = await evaluate(
    `window.__ft.uploadPattern(${JSON.stringify(name)}, ${JSON.stringify(FT_TARGET_DIR)}, ${FT_CANCEL_E2E_BYTES})`,
    FT_TIMEOUT_MS,
  )
  await evaluate(`window.__ft.cancel(${Number(started.jobId)})`, 5000)
  const cancelled = await waitFtCancelled(started.jobId)
  console.log(`FT_CANCEL_E2E: PASS ${JSON.stringify({
    jobId: started.jobId,
    state: cancelled.state,
    transferred: cancelled.transferred,
    remotePath: targetPath,
  })}`)
  return { jobId: started.jobId, state: cancelled.state, remotePath: targetPath }
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
    if (m.method === 'Network.responseReceived') {
      const response = m.params?.response
      if (response?.url?.includes('/alloc/local/rtc')) {
        rtcSignalHttpStatuses.push(response.status)
        void cmd('Network.getResponseBody', { requestId: m.params.requestId }, 5000)
          .then((body) => {
            const code = JSON.parse(body.body || '{}').code
            if (Number.isInteger(code)) rtcSignalResponseCodes.push(code)
          })
          .catch(() => {})
      }
    }
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
  await cmd('Network.enable')
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
  if (TAKEOVER_CONFIRMATION !== 'default') {
    const accepted = TAKEOVER_CONFIRMATION === 'accept'
    await cmd('Page.addScriptToEvaluateOnNewDocument', {
      source: `window.confirm = () => ${accepted ? 'true' : 'false'}`,
    })
  }
  // Under long repeated headless runs Chrome can spend more than ten seconds
  // committing a navigation while cleaning up the previous GPU/network
  // processes.  This is test-runner startup, before any RTC gate is reached;
  // give navigation the same 30-second budget as the connection phase.
  await cmd('Page.navigate', { url: PAGE_URL }, 30000)

  console.log('RTC_PHASE: waiting-connected')
  let connected = false
  const connectDeadline = Date.now() + CONNECT_TIMEOUT_SECONDS * 1000
  while (Date.now() < connectDeadline) {
    const remaining = Math.max(1, connectDeadline - Date.now())
    const st = await evaluate(
      `window.__pc ? window.__pc.connectionState : 'none'`, Math.min(1000, remaining),
    ).catch(() => 'err')
    if (st === 'connected') { connected = true; break }
    await sleep(500)
  }
  if (!QUIET) console.log('connected:', connected)
  if (!connected) {
    const detail = await evaluate(`({
      status: window.__conn?.status?.() || 'unknown',
      rtcMode: window.__conn?.rtcMode?.() || 'unknown',
      body: (document.body?.innerText || '').slice(-2000),
    })`).catch(() => null)
    if (rtcSignalHttpStatuses.includes(403) || rtcSignalResponseCodes.includes(704) ||
        detail?.body?.includes('设备已被连接,未接管')) {
      console.error('RTC_OCCUPIED_REJECTED')
    }
    throw new Error(`未连接: ${JSON.stringify(detail)}`)
  }

  let prev = null
  let first = null
  let last = null
  let lastWithPair = null
  let finalSample = null
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
    finalSample = s
  }
  if (!first?.inbound || !last?.inbound) throw new Error('观测窗口缺少视频统计')
  // `last` intentionally retains the most recent valid media sample for
  // bitrate calculations. It must not, however, hide a peer that vanished
  // near the end of the observation window (notably after takeover).
  if (!finalSample?.inbound || !finalSample?.pair || !finalSample?.localCand || !finalSample?.remoteCand) {
    throw new Error(`观测结束时 RTC 媒体或候选统计缺失: ${JSON.stringify(finalSample)}`)
  }
  if (!lastWithPair?.pair) throw new Error('观测窗口从未出现 selected candidate 统计')
  if (finalSample.conn !== 'connected' || !['connected', 'completed'].includes(finalSample.ice)) {
    throw new Error(`观测结束时 RTC 已断开: conn=${finalSample.conn} ice=${finalSample.ice}`)
  }
  const frameDelta = last.inbound.dec - first.inbound.dec
  const staticFrameHold = frameDelta === 0 &&
    last.inbound.bytes === first.inbound.bytes &&
    (first.inbound.dec || 0) > 0 &&
    (first.inbound.frameWidth || 0) > 0 &&
    (first.inbound.frameHeight || 0) > 0
  // Desktop capture is content-adaptive: a nearly static desktop may emit
  // only a handful of frames in a long sample. Any decoded-frame progress is
  // therefore valid; zero progress is valid only for an unchanged, already
  // decoded frame whose RTP byte count also remains unchanged.
  if (frameDelta === 0 && !staticFrameHold) {
    throw new Error(`视频解码停止但 RTP 状态仍在变化: ${frameDelta}`)
  }
  const observedLost = (last.inbound.lost || 0) - (first.inbound.lost || 0)
  const observedFreezes = (last.inbound.freezeCount || 0) - (first.inbound.freezeCount || 0)
  const observedFreezeDuration =
    (last.inbound.totalFreezesDuration || 0) - (first.inbound.totalFreezesDuration || 0)
  // Chromium's freeze metrics assume a continuously paced camera. Desktop
  // capture is content-adaptive and may intentionally emit one frame every
  // several seconds for an unchanged screen, which Chrome counts almost
  // entirely as freeze time even though fresh decoded frames/bytes continue.
  // Keep those values as quality evidence; liveness is gated above by decoded
  // progress or a coherent static-frame hold. The deterministic LAN transport
  // failure here is newly lost RTP packets.
  if (observedLost > 0) {
    throw new Error(`LAN 稳态观测窗口存在新增丢包: lostDelta=${observedLost} freezeDelta=${observedFreezes} freezeDurationDelta=${observedFreezeDuration}`)
  }
  const selected = [lastWithPair.localCand, lastWithPair.remoteCand].filter(Boolean)
  if (EXPECT_CANDIDATE_TYPE && !selected.some((candidate) => candidate.type === EXPECT_CANDIDATE_TYPE)) {
    throw new Error(`selected candidate 不是 ${EXPECT_CANDIDATE_TYPE}: ${JSON.stringify(selected)}`)
  }
  if (EXPECT_RELAY_PROTOCOL && !selected.some((candidate) =>
    candidate.type === 'relay' && candidate.relayProto === EXPECT_RELAY_PROTOCOL)) {
    throw new Error(`selected TURN transport 不是 ${EXPECT_RELAY_PROTOCOL}: ${JSON.stringify(selected)}`)
  }
  const fileTransfer = await runFileTransferE2E()
  const fileTransferCancel = await runFileTransferCancelE2E()
  console.log('RESULT: PASS', JSON.stringify({
    frameDelta,
    staticFrameHold,
    startupLost: first.inbound.lost || 0,
    observedLost,
    observedFreezes,
    observedFreezeDuration,
    pairSamples,
    missingPairSamples,
    resolution: `${last.inbound.frameWidth}x${last.inbound.frameHeight}`,
    rttMs: (lastWithPair.pair.rtt || 0) * 1000,
    localCandidate: lastWithPair.localCand,
    remoteCandidate: lastWithPair.remoteCand,
    fileTransfer,
    fileTransferCancel,
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
