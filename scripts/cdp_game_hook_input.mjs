// Headless verify: game-hook render → web_client auto-connect → dispatch mouse/keyboard
// events via CDP → host 端 DLL 应收到并合成 RawInput（检查 px_gh_<port>.log）。
// Usage: RENDER_PORT=32101 DEVICE_ID=e2e-machine-1 node scripts/cdp_game_hook_input.mjs
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const CHROME = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PORT = process.env.RENDER_PORT || '32000'
const DEVICE_ID = process.env.DEVICE_ID || 'debug1'
const PAGE_URL = process.env.WEB_URL
  || `http://127.0.0.1:${PORT}/web_client/?deviceId=${encodeURIComponent(DEVICE_ID)}`
const CDP_PORT = Number(process.env.CDP_PORT || 9334)
const REQUIRE_AUDIO = process.env.REQUIRE_AUDIO === '1'
const WAIT_CONNECT_MS = 40000

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const __dirname = path.dirname(fileURLToPath(import.meta.url))
const profile = path.join(os.tmpdir(), `cdp-game-input-${Date.now()}`)

const pageForLog = new URL(PAGE_URL)
if (pageForLog.hash) pageForLog.hash = '#<redacted>'
console.log('[cdp] page:', pageForLog.toString())

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

const STATE_JS = `(async () => {
  const pc = window.__pc
  const v = document.querySelector('video')
  const r = v ? v.getBoundingClientRect() : null
  let framesDecoded = 0
  let framesReceived = 0
  if (pc) {
    const stats = await pc.getStats()
    stats.forEach((s) => {
      if (s.type === 'inbound-rtp' && (s.kind === 'video' || s.mediaType === 'video')) {
        framesDecoded = Math.max(framesDecoded, s.framesDecoded || 0)
        framesReceived = Math.max(framesReceived, s.framesReceived || 0)
      }
    })
  }
  return {
    conn: pc ? pc.connectionState : null,
    tag: document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? '',
    rect: r ? { x: r.x, y: r.y, w: r.width, h: r.height } : null,
    videoWidth: v?.videoWidth ?? 0,
    videoHeight: v?.videoHeight ?? 0,
    readyState: v?.readyState ?? -1,
    framesDecoded,
    framesReceived,
    receiverKinds: pc ? pc.getReceivers().map((receiver) => receiver.track?.kind).filter(Boolean) : [],
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
  let state = null
  let connected = false
  while (Date.now() - t0 < WAIT_CONNECT_MS) {
    state = await evaluate(STATE_JS).catch((e) => ({ err: String(e) }))
    console.log('[cdp] connect poll:', JSON.stringify(state))
    if (state && (state.tag === '已连接' || state.conn === 'connected')) {
      connected = true
      break
    }
    await sleep(1000)
  }
  if (!connected) throw new Error('自动连接失败(无头未进入已连接)')
  if (!state.rect || state.rect.w < 10) throw new Error('找不到 video 元素区域')

  // PeerConnection can become connected slightly before the first decoded frame.
  // Require an actual video frame, not merely a signaling success.
  let videoReady = state.videoWidth > 0 || state.framesDecoded > 0 || state.framesReceived > 5
  for (let i = 0; i < 45 && !videoReady; i++) {
    await sleep(1000)
    state = await evaluate(STATE_JS)
    videoReady = state.videoWidth > 0 || state.framesDecoded > 0 || state.framesReceived > 5
  }
  if (!videoReady) throw new Error('已连接但未收到可解码视频帧')
  if (REQUIRE_AUDIO && !state.receiverKinds.includes('audio')) {
    throw new Error('已连接但未收到音频轨道')
  }
  console.log('[cdp] media ready:', JSON.stringify({
    video: `${state.videoWidth}x${state.videoHeight}`,
    framesDecoded: state.framesDecoded,
    framesReceived: state.framesReceived,
    receiverKinds: state.receiverKinds,
  }))

  // 等输入回传初始化完成（initInput 要轮询 monitor 名,最长 ~15s）
  let attached = false
  for (let i = 0; i < 30; i++) {
    attached = await evaluate(`!!(window.__input && window.__input.attached && window.__input.attached())`)
    if (attached) break
    await sleep(1000)
  }
  console.log('[cdp] input attached:', attached)
  if (!attached) throw new Error('输入回传未就绪(__input.attached=false)')

  // 通过页面自带 __input.testSend 走真实发送链路（CDP/DOM 合成事件的 movementX 不可靠）
  const DISPATCH_JS = `(() => {
    if (!window.__input || !window.__input.testSend) return { ok: false, reason: '__input missing' }
    const results = []
    for (let i = 0; i < 10; i++) {
      results.push(window.__input.testSend({
        x: 0.30 + i * 0.04,
        y: 0.30 + i * 0.03,
        keyCode: i === 0 ? 'KeyW' : 'None',
      }))
    }
    return { ok: true, first: results[0], count: results.length }
  })()`
  const dr = await evaluate(DISPATCH_JS)
  console.log('[cdp] testSend x10 (move path + 1 click + 1 keyW):', JSON.stringify(dr))

  // 给事件传输留点时间
  await sleep(1500)
  const probe = await evaluate(`(() => {
    const i = window.__input
    if (!i) return null
    return {
      domMoveEvents: i.domMoveEvents,
      sentMessages: i.sentMessages,
      viewOnly: i.viewOnly,
      dcState: i.opts?.dc?.readyState,
      lastMouse: i.lastMouse,
    }
  })()`)
  console.log('[cdp] input probe:', JSON.stringify(probe))
  console.log('[cdp] OK: video/audio received and input events dispatched')
  process.exit(0)
}

main().catch((e) => {
  console.error('[cdp] ERROR:', e)
  process.exit(1)
})
