// Quick WebRTC audio inbound probe (HTTP localhost).
// Verifies whether the browser receives audio RTP bytes (independent of speaker unmute).
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  process.env.WEB_URL ||
  `http://127.0.0.1:${process.env.RENDER_PORT || '32000'}/web_client/?deviceId=${encodeURIComponent(process.env.DEVICE_ID || 'debug1')}`
const CDP_PORT = Number(process.env.CDP_PORT || 9225)
const SAMPLE_SECONDS = Number(process.env.SAMPLE_SECONDS || 12)

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
        bytes: s.bytesReceived,
        pkts: s.packetsReceived,
        lost: s.packetsLost,
        jitter: s.jitter,
        audioLevel: s.audioLevel,
        totalAudioEnergy: s.totalAudioEnergy,
        conf: s.decoderImplementation,
      }
    }
    if (s.type === 'inbound-rtp' && s.kind === 'video') {
      out.videoInbound = {
        bytes: s.bytesReceived,
        pkts: s.packetsReceived,
        fps: s.framesPerSecond,
        w: s.frameWidth,
        h: s.frameHeight,
      }
    }
  })
  return out
})()`

async function main() {
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
  for (let i = 0; i < 40; i++) {
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

  // Unmute in page (userGesture already set on evaluate)
  await evaluate(`(() => {
    const v = document.querySelector('video');
    if (v) { v.muted = false; try { v.play() } catch(e) {} }
    return { muted: !!v?.muted, paused: !!v?.paused };
  })()`)

  let prev = null
  for (let t = 0; t < SAMPLE_SECONDS; t += 3) {
    await sleep(3000)
    const s = await evaluate(SNAP_JS).catch((e) => ({ err: e.message }))
    if (s.audioInbound && prev?.audioInbound) {
      const dt = 3
      s.audioCalc = {
        kbps: Math.round(((s.audioInbound.bytes - prev.audioInbound.bytes) * 8) / dt / 1000),
        pps: Math.round((s.audioInbound.pkts - prev.audioInbound.pkts) / dt),
      }
    }
    console.log(`[t=${t + 3}s]`, JSON.stringify(s))
    prev = s
  }
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
