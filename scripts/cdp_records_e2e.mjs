// E2E for CMS web device records page (topology 1 direct).
// Usage: node scripts/cdp_records_e2e.mjs
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
import fs from 'node:fs'

const CHROME = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE = process.env.CMS_URL || 'http://10.0.0.16:30500'
const APPKEY = process.env.APPKEY || '49727717a74720a863f007dcdb13324e'
const DEVICE = process.env.DEVICE_ID || '001190520'
const PLAY_FILE = process.env.PLAY_FILE || '' // pick the row whose name contains this
const WAIT_VIDEO_MS = Number(process.env.WAIT_VIDEO_MS || 10000)
const OUT = process.env.OUT || `records_e2e_${Date.now()}.png`
const CDP_PORT = Number(process.env.CDP_PORT || 9491)

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-rec-${Date.now()}`)
const chrome = spawn(CHROME, [
  '--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`,
  '--no-first-run', '--window-size=1600,900',
  'about:blank',
], { stdio: 'ignore' })
process.on('exit', () => { try { chrome.kill() } catch { /* ignore */ } })

let msgId = 0
const pending = new Map()
let ws
const consoleErrors = []
const netReqs = new Map() // requestId -> url
const netDone = []        // {url, status}

function cmd(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    pending.set(id, { resolve, reject })
    ws.send(JSON.stringify({ id, method, params }))
  })
}
async function evaluate(expression) {
  const r = await cmd('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true })
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 800))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) return } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools not ready')
}
async function waitFor(expr, timeoutMs = 15000, label = expr) {
  const t0 = Date.now()
  while (Date.now() - t0 < timeoutMs) {
    try { if (await evaluate(expr)) return true } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('timeout waiting: ' + label)
}

await waitDevtools()
const r0 = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(BASE + '/')}`, { method: 'PUT' })
const target = await r0.json()
ws = new WebSocket(target.webSocketDebuggerUrl)
ws.onmessage = (ev) => {
  const m = JSON.parse(ev.data)
  if (m.id && pending.has(m.id)) {
    const p = pending.get(m.id)
    pending.delete(m.id)
    m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result)
    return
  }
  if (m.method === 'Runtime.exceptionThrown') {
    consoleErrors.push('EXC: ' + JSON.stringify(m.params.exceptionDetails).slice(0, 300))
  }
  if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') {
    consoleErrors.push('CONSOLE: ' + m.params.args.map(a => a.value ?? a.description ?? '').join(' ').slice(0, 300))
  }
  if (m.method === 'Network.requestWillBeSent') {
    netReqs.set(m.params.requestId, m.params.request.url)
  }
  if (m.method === 'Network.responseReceived') {
    const url = netReqs.get(m.params.requestId)
    if (url) netDone.push({ url, status: m.params.response.status })
  }
}
await new Promise((res) => { ws.onopen = res })
await cmd('Runtime.enable')
await cmd('Network.enable')
await cmd('Page.enable')

// seed login state, then open the records page
await evaluate(`localStorage.setItem('token','e2e'); localStorage.setItem('appkey','${APPKEY}'); localStorage.setItem('username','e2e'); 'ok'`)
await cmd('Page.navigate', { url: `${BASE}/records/${DEVICE}` })

// 1. list renders
await waitFor(`document.querySelectorAll('.ant-table-row').length >= 3`, 20000, 'table rows >= 3')
const rowInfo = await evaluate(`[...document.querySelectorAll('.ant-table-row')].map(r => r.querySelector('span').textContent)`)
console.log('ROWS:', JSON.stringify(rowInfo))

// topology badge text near title
const topo = await evaluate(`(document.querySelector('.ant-card-head')?.innerText || '').replace(/\\n/g,' | ')`)
console.log('HEADER:', topo)

// 2. click the 播放 button of the target row (first row by default)
const btnInfo = await evaluate(`(function(){
  const want = ${JSON.stringify(PLAY_FILE)}
  const rows = [...document.querySelectorAll('.ant-table-row')]
  const row = want ? rows.find(r => r.innerText.includes(want)) : rows[0]
  if (!row) return { found: false }
  const btn = [...row.querySelectorAll('button')].find(b => /播\\s*放/.test(b.textContent))
  if (!btn) return { found: true, hasPlay: false }
  btn.click()
  return { found: true, hasPlay: true, disabled: btn.disabled }
})()`)
console.log('PLAY_CLICK:', JSON.stringify(btnInfo))
await waitFor(`!!document.querySelector('video')`, WAIT_VIDEO_MS, 'video element')
const videoSrc = await evaluate(`document.querySelector('video').src`)
console.log('VIDEO_SRC:', videoSrc)

// 3. wait playable, then seek (Range request must appear)
await waitFor(`document.querySelector('video').readyState >= 2`, 20000, 'video readyState>=2')
const dur = await evaluate(`document.querySelector('video').duration`)
console.log('DURATION:', dur)
const seekTo = Math.max(1, Math.floor(dur * 0.6))
await evaluate(`new Promise((res, rej) => { const v = document.querySelector('video'); const t = setTimeout(() => rej(new Error('seek timeout')), 10000); v.addEventListener('seeked', () => { clearTimeout(t); res(v.currentTime) }, { once: true }); v.currentTime = ${seekTo} })`)
console.log('SEEK_OK to', seekTo)
await sleep(1500)

// 4. verify video data requests succeeded (direct: panel 206; cms topo: uploads 200/206)
const panelReqs = netDone.filter(r => r.url.includes('10.0.0.90:20369'))
const cmsReqs = netDone.filter(r => r.url.includes('/uploads/records/'))
console.log('PANEL_REQS:', JSON.stringify(panelReqs))
console.log('CMS_UPLOAD_REQS:', JSON.stringify(cmsReqs))
const has206 = panelReqs.some(r => r.status === 206) || cmsReqs.some(r => r.status === 206 || r.status === 200)
console.log('HAS_206:', has206)

// 5. screenshot
const shot = await cmd('Page.captureScreenshot', { format: 'png' })
fs.writeFileSync(OUT, Buffer.from(shot.data, 'base64'))
console.log('SHOT:', OUT)

console.log('CONSOLE_ERRORS:', consoleErrors.length ? JSON.stringify(consoleErrors.slice(0, 10)) : 'none')
console.log(has206 ? 'E2E_PASS' : 'E2E_FAIL')
process.exit(has206 ? 0 : 1)
