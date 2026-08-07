// Headless WebRTC audio inbound probe.
// Reports whether the browser receives growing audio RTP + non-silent levels.
//
// Env:
//   RENDER_PORT / DEVICE_ID / WEB_URL / CDP_PORT / SAMPLE_SECONDS
// Exit 0 only when heard=true (bytes growing AND energy/level evidence).
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  process.env.WEB_URL ||
  `http://127.0.0.1:${process.env.RENDER_PORT || '32000'}/web_client/?deviceId=${encodeURIComponent(process.env.DEVICE_ID || 'debug1')}`
const CDP_PORT = Number(process.env.CDP_PORT || 9225)
const SAMPLE_SECONDS = Number(process.env.SAMPLE_SECONDS || 15)
const MODE = process.env.AUDIO_MODE || 'unknown'

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-audio-${Date.now()}`)
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
process.on('exit', () => {
  try {
    chrome.kill()
  } catch {
    /* ignore */
  }
})

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
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 400))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (r.ok) return
    } catch {
      /* retry */
    }
    await sleep(500)
  }
  throw new Error('devtools 未就绪')
}

const SNAP_JS = `(async () => {
  const pc = window.__pc
  if (!pc) return { err: 'no __pc' }
  const video = document.querySelector('video')
  const stream = video?.srcObject
  const tracks = stream
    ? stream.getTracks().map((t) => ({ kind: t.kind, id: t.id, enabled: t.enabled, muted: t.muted, state: t.readyState }))
    : []
  const stats = await pc.getStats()
  const out = {
    conn: pc.connectionState,
    ice: pc.iceConnectionState,
    pageMuted: !!video?.muted,
    videoPaused: !!video?.paused,
    tracks,
    audioInbound: null,
    videoInbound: null,
  }
  stats.forEach((s) => {
    if (s.type === 'inbound-rtp' && s.kind === 'audio') {
      out.audioInbound = {
        bytes: s.bytesReceived || 0,
        pkts: s.packetsReceived || 0,
        lost: s.packetsLost || 0,
        jitter: s.jitter || 0,
        audioLevel: s.audioLevel ?? null,
        totalAudioEnergy: s.totalAudioEnergy ?? null,
        conf: s.decoderImplementation,
      }
    }
    if (s.type === 'inbound-rtp' && s.kind === 'video') {
      out.videoInbound = {
        bytes: s.bytesReceived || 0,
        pkts: s.packetsReceived || 0,
        fps: s.framesPerSecond || 0,
        w: s.frameWidth || 0,
        h: s.frameHeight || 0,
      }
    }
  })
  return out
})()`

function dbFromLevel(level) {
  if (level == null || !(level > 0)) return -120
  return 20 * Math.log10(level)
}

async function main() {
  console.log('MODE=', MODE)
  console.log('PAGE_URL=', PAGE_URL)
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, {
    method: 'PUT',
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
  await new Promise((res, rej) => {
    ws.onopen = res
    ws.onerror = rej
  })
  await cmd('Runtime.enable')

  let connected = false
  for (let i = 0; i < 50; i++) {
    const st = await evaluate(`window.__pc ? window.__pc.connectionState : 'none'`).catch(() => 'err')
    if (st === 'connected') {
      connected = true
      break
    }
    await sleep(500)
  }
  console.log('connected:', connected)
  if (!connected) {
    const logs = await evaluate(
      `Array.isArray(window.__logs) ? window.__logs.slice(-20) : (document.body?.innerText||'').slice(0,500)`,
    ).catch((e) => e.message)
    console.log('diag:', logs)
    throw new Error('未连接')
  }

  await evaluate(`(() => {
    const v = document.querySelector('video');
    if (v) { v.muted = false; try { v.play() } catch(e) {} }
    return { muted: !!v?.muted, paused: !!v?.paused };
  })()`)

  let first = null
  let last = null
  let maxLevel = 0
  let maxKbps = 0
  for (let t = 0; t < SAMPLE_SECONDS; t += 3) {
    await sleep(3000)
    const s = await evaluate(SNAP_JS).catch((e) => ({ err: e.message }))
    if (!first && s.audioInbound) first = s
    if (s.audioInbound) {
      last = s
      const lvl = s.audioInbound.audioLevel || 0
      if (lvl > maxLevel) maxLevel = lvl
    }
    if (s.audioInbound && first?.audioInbound) {
      const dt = t + 3
      if (dt > 0) {
        const kbps = Math.round(((s.audioInbound.bytes - first.audioInbound.bytes) * 8) / dt / 1000)
        if (kbps > maxKbps) maxKbps = kbps
        s.audioCalc = {
          kbpsFromStart: kbps,
          bytesDelta: s.audioInbound.bytes - first.audioInbound.bytes,
          energyDelta:
            (s.audioInbound.totalAudioEnergy ?? 0) - (first.audioInbound.totalAudioEnergy ?? 0),
          levelDb: dbFromLevel(s.audioInbound.audioLevel),
        }
      }
    }
    console.log(`[t=${t + 3}s]`, JSON.stringify(s))
  }

  const bytesDelta = (last?.audioInbound?.bytes || 0) - (first?.audioInbound?.bytes || 0)
  const energyDelta =
    (last?.audioInbound?.totalAudioEnergy ?? 0) - (first?.audioInbound?.totalAudioEnergy ?? 0)
  const levelDb = dbFromLevel(maxLevel)
  // RTP growing + (energy rising OR level above silence floor)
  const rtpOk = bytesDelta > 2000
  const signalOk = energyDelta > 1e-6 || maxLevel > 0.001
  const heard = rtpOk && signalOk
  const summary = {
    mode: MODE,
    heard,
    rtpOk,
    signalOk,
    bytesDelta,
    energyDelta,
    maxLevel,
    levelDbApprox: Number(levelDb.toFixed(1)),
    maxKbpsApprox: maxKbps,
    videoFps: last?.videoInbound?.fps || 0,
  }
  console.log('SUMMARY', JSON.stringify(summary))
  if (!heard) {
    throw new Error(`heard=false mode=${MODE} bytesDelta=${bytesDelta} energyDelta=${energyDelta} maxLevel=${maxLevel}`)
  }
  console.log(`PASS mode=${MODE} heard=true`)
}

main()
  .catch((e) => {
    console.error('异常:', e.message)
    process.exitCode = 1
  })
  .finally(() => {
    try {
      ws?.close()
    } catch {
      /* ignore */
    }
    try {
      chrome.kill()
    } catch {
      /* ignore */
    }
    setTimeout(() => process.exit(process.exitCode ?? 0), 500).unref()
  })
