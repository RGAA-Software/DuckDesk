// Headless: connect web_client and dump WebRTC jitter-buffer / processing latency.
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const CHROME = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PORT = process.env.RENDER_PORT || '32000'
const DEVICE_ID = process.env.DEVICE_ID || 'debug1'
const PAGE_URL =
  `http://127.0.0.1:${PORT}/web_client/?deviceId=${encodeURIComponent(DEVICE_ID)}`
const CDP_PORT = Number(process.env.CDP_PORT || 9334)
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-latency-${Date.now()}`)

console.log('[lat] page:', PAGE_URL)

const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--autoplay-policy=no-user-gesture-required',
    'about:blank',
  ],
  { stdio: 'ignore' },
)
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
  const r = await cmd('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  })
  if (r.exceptionDetails) {
    throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 500))
  }
  return r.result?.value
}

async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (r.ok) return
    } catch { /* retry */ }
    await sleep(300)
  }
  throw new Error('devtools not ready')
}

const SNAP_JS = `(async () => {
  const pc = window.__pc
  const v = document.querySelector('video')
  if (!pc) {
    return {
      tag: document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? '',
      conn: null,
      hasPc: false,
    }
  }
  const receivers = pc.getReceivers()
    .filter((r) => r.track?.kind === 'video')
    .map((r) => ({
      jitterBufferTarget: r.jitterBufferTarget,
      playoutDelayHint: r.playoutDelayHint,
      trackState: r.track?.readyState,
      muted: r.track?.muted,
    }))

  let inbound = null
  const stats = await pc.getStats()
  stats.forEach((s) => {
    if (s.type === 'inbound-rtp' && (s.kind === 'video' || s.mediaType === 'video')) {
      const emitted = s.jitterBufferEmittedCount || 0
      inbound = {
        fps: s.framesPerSecond ?? 0,
        framesDecoded: s.framesDecoded ?? 0,
        width: s.frameWidth ?? 0,
        height: s.frameHeight ?? 0,
        jbDelayMs: emitted > 0 ? (s.jitterBufferDelay / emitted) * 1000 : null,
        jbTargetMs: emitted > 0 ? ((s.jitterBufferTargetDelay || 0) / emitted) * 1000 : null,
        decodeMs: s.framesDecoded > 0 ? (s.totalDecodeTime / s.framesDecoded) * 1000 : null,
        procMs: s.framesDecoded > 0 ? (s.totalProcessingDelay / s.framesDecoded) * 1000 : null,
        jitterMs: (s.jitter || 0) * 1000,
        decoder: s.decoderImplementation ?? '',
      }
    }
  })
  return {
    hasPc: true,
    conn: pc.connectionState,
    ice: pc.iceConnectionState,
    tag: document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? '',
    video: { w: v?.videoWidth ?? 0, h: v?.videoHeight ?? 0, rs: v?.readyState ?? -1 },
    receivers,
    inbound,
  }
})()`

async function main() {
  for (let i = 0; i < 40; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${PORT}/api/ping`)
      if (r.ok) break
    } catch { /* */ }
    if (i === 39) throw new Error('render not ready')
    await sleep(500)
  }
  await waitDevtools()

  const r = await fetch(
    `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`,
    { method: 'PUT' },
  )
  if (!r.ok) throw new Error(`open page failed: ${r.status}`)
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
  await cmd('Page.enable')

  const t0 = Date.now()
  let last = null
  while (Date.now() - t0 < 50000) {
    last = await evaluate(SNAP_JS).catch((e) => ({ err: String(e) }))
    console.log('[lat]', JSON.stringify(last))
    if (last?.inbound?.framesDecoded > 60 && last?.inbound?.jbTargetMs != null) {
      // wait for keepalive ticks to pin jitterBufferTarget
      await sleep(3000)
      last = await evaluate(SNAP_JS)
      console.log('[lat] settled:', JSON.stringify(last))
      break
    }
    await sleep(1500)
  }

  if (!last?.inbound) {
    console.error('[lat] FAIL: no inbound video stats')
    process.exit(2)
  }
  const { jbTargetMs, jbDelayMs, procMs, decodeMs } = last.inbound
  console.log('[lat] SUMMARY', {
    jbTargetMs,
    jbDelayMs,
    procMs,
    decodeMs,
    receivers: last.receivers,
  })
  if ((jbTargetMs ?? 0) > 200 || (jbDelayMs ?? 0) > 250) {
    console.error('[lat] HIGH_BUFFER: app keepalive did not pin jitter buffer low')
    process.exit(3)
  }
  console.log('[lat] OK: buffer looks low (app path, no CDP force-set)')
  process.exit(0)
}

main().catch((e) => {
  console.error('[lat] ERROR:', e)
  process.exit(1)
})
