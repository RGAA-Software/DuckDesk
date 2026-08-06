// Headless verify: game-hook render → web_client auto-connect → video frames.
// Assumes GammaRayRender already listening on 20371 (see run_game_hook_render.bat).
// Usage: node scripts/cdp_game_hook_video.mjs
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
const CDP_PORT = Number(process.env.CDP_PORT || 9333)
const WAIT_CONNECT_MS = 40000
const WAIT_VIDEO_MS = 45000

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const __dirname = path.dirname(fileURLToPath(import.meta.url))
const profile = path.join(os.tmpdir(), `cdp-game-hook-${Date.now()}`)

console.log('[cdp] page:', PAGE_URL)
console.log('[cdp] headless chrome + remote-debugging-port', CDP_PORT)

const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--disable-gpu',
    'about:blank',
  ],
  { stdio: 'ignore' },
)
process.on('exit', () => {
  try { chrome.kill() } catch { /* ignore */ }
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
  if (r.exceptionDetails) {
    throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 400))
  }
  return r.result?.value
}

async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (r.ok) return
    } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools 未就绪')
}

async function waitRender() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${PORT}/api/ping`)
      if (r.ok) return
    } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error(`render http://${PORT} 未就绪`)
}

const VIDEO_JS = `(async () => {
  const v = document.querySelector('video')
  const pc = window.__pc
  let fps = null
  let framesReceived = null
  let framesDecoded = null
  let conn = pc ? pc.connectionState : null
  if (pc) {
    const stats = await pc.getStats()
    stats.forEach((s) => {
      if (s.type === 'inbound-rtp' && (s.kind === 'video' || s.mediaType === 'video')) {
        fps = s.framesPerSecond ?? fps
        framesReceived = s.framesReceived ?? framesReceived
        framesDecoded = s.framesDecoded ?? framesDecoded
      }
    })
  }
  return {
    conn,
    tag: document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? '',
    videoWidth: v?.videoWidth ?? 0,
    videoHeight: v?.videoHeight ?? 0,
    readyState: v?.readyState ?? -1,
    fps,
    framesReceived,
    framesDecoded,
  }
})()`

async function main() {
  await waitRender()
  await waitDevtools()

  const r = await fetch(
    `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`,
    { method: 'PUT' },
  )
  if (!r.ok) throw new Error(`open page failed: HTTP ${r.status}`)
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
  await cmd('Page.enable')

  const t0 = Date.now()
  let connected = false
  while (Date.now() - t0 < WAIT_CONNECT_MS) {
    const snap = await evaluate(VIDEO_JS).catch((e) => ({ err: String(e) }))
    console.log('[cdp] connect poll:', JSON.stringify(snap))
    if (snap && (snap.tag === '已连接' || snap.conn === 'connected')) {
      connected = true
      break
    }
    await sleep(1000)
  }
  if (!connected) throw new Error('自动连接失败(无头未进入已连接)')

  const t1 = Date.now()
  let hasVideo = false
  let last = null
  while (Date.now() - t1 < WAIT_VIDEO_MS) {
    last = await evaluate(VIDEO_JS).catch((e) => ({ err: String(e) }))
    console.log('[cdp] video poll:', JSON.stringify(last))
    if (
      last &&
      ((last.videoWidth > 0 && last.videoHeight > 0) ||
        (last.framesDecoded ?? 0) > 0 ||
        (last.framesReceived ?? 0) > 5 ||
        (last.fps ?? 0) > 0)
    ) {
      hasVideo = true
      break
    }
    await sleep(1500)
  }

  if (!hasVideo) {
    console.error('[cdp] FAIL: connected but no video frames')
    console.error('[cdp] last:', JSON.stringify(last))
    process.exit(2)
  }
  console.log('[cdp] OK: auto-connected and video present')
  console.log('[cdp] last:', JSON.stringify(last))
  process.exit(0)
}

main().catch((e) => {
  console.error('[cdp] ERROR:', e)
  process.exit(1)
})
