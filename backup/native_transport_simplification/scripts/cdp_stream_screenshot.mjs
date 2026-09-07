// Headless screenshot of a remote web_client stream (for visual diagnosis).
// Usage: WEB_URL="http://10.0.0.70:32001/web_client/?deviceId=990405157&instanceId=inst-17-3e2a5ddf" OUT=shot.png node scripts/cdp_stream_screenshot.mjs
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
import fs from 'node:fs'

const CHROME = process.env.CHROME_PATH
  || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL = process.env.WEB_URL
const OUT = process.env.OUT || `stream_shot_${Date.now()}.png`
const CDP_PORT = Number(process.env.CDP_PORT || 9444)
const WAIT_VIDEO_MS = Number(process.env.WAIT_VIDEO_MS || 60000)
const SETTLE_MS = Number(process.env.SETTLE_MS || 8000)

if (!PAGE_URL) {
  console.error('WEB_URL required')
  process.exit(1)
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-shot-${Date.now()}`)
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`, '--no-first-run', '--window-size=1600,900', 'about:blank'], { stdio: 'ignore' })
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
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 400))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) return } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools not ready')
}

const VIDEO_JS = `(() => {
  const v = document.querySelector('video')
  const pc = window.__pc
  return {
    conn: pc ? pc.connectionState : null,
    tag: document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? '',
    videoWidth: v?.videoWidth ?? 0,
    videoHeight: v?.videoHeight ?? 0,
    readyState: v?.readyState ?? -1,
  }
})()`

async function main() {
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, { method: 'PUT' })
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
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej })
  await cmd('Runtime.enable')
  await cmd('Page.enable')

  const t0 = Date.now()
  let snap = null
  while (Date.now() - t0 < WAIT_VIDEO_MS) {
    snap = await evaluate(VIDEO_JS).catch((e) => ({ err: String(e) }))
    console.log('[cdp] poll:', JSON.stringify(snap))
    if (snap && snap.videoWidth > 0 && snap.readyState >= 2) break
    await sleep(1500)
  }
  if (!snap || snap.videoWidth <= 0) {
    console.error('[cdp] FAIL: no video within timeout')
    process.exit(2)
  }
  console.log('[cdp] video up, settling', SETTLE_MS, 'ms ...')
  await sleep(SETTLE_MS)

  const shot = await cmd('Page.captureScreenshot', { format: 'png' })
  fs.writeFileSync(OUT, Buffer.from(shot.data, 'base64'))
  console.log('[cdp] screenshot saved:', OUT)
  process.exit(0)
}

main().catch((e) => { console.error('[cdp] ERROR:', e); process.exit(1) })
