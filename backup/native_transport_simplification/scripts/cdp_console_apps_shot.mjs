// Headless screenshot of Console web /apps page (node feature verification).
// Usage: OUT=/tmp/console_apps.png EXPAND=1 node scripts/_tmp_console_apps_shot.mjs
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
import fs from 'node:fs'

const CHROME = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE = process.env.CONSOLE_URL || process.env.CMS_URL || 'https://10.0.0.16:30500'
const APPKEY = process.env.APPKEY || '49727717a74720a863f007dcdb13324e'
const OUT = process.env.OUT || `console_apps_${Date.now()}.png`
const CDP_PORT = Number(process.env.CDP_PORT || 9490)
const EXPAND = process.env.EXPAND === '1'
const CLICK_NODES_APP = process.env.CLICK_NODES_APP || '' // app name; click its「N 个节」to open the node dialog
const CLICK_PAGE_2 = process.env.CLICK_PAGE_2 === '1'
const CLICK_START_APP = process.env.CLICK_START_APP || '' // app name; click its 启动 to capture the error toast

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-console-${Date.now()}`)
const chrome = spawn(CHROME, [
  '--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`,
  '--no-first-run', '--window-size=1600,900', '--ignore-certificate-errors', 'about:blank',
], { stdio: 'ignore' })
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
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 600))
  return r.result?.value
}
async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) return } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools not ready')
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
  }
}
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej })
await cmd('Runtime.enable')
await cmd('Page.enable')

await sleep(3000)
const seeded = await evaluate(`(() => {
  localStorage.setItem('appkey', '${APPKEY}')
  localStorage.setItem('token', 'e2e')
  localStorage.setItem('username', '70')
  location.hash = ''
  return { origin: location.origin, token: localStorage.getItem('token') }
})()`)
console.log('[seed]', JSON.stringify(seeded))
await cmd('Page.navigate', { url: BASE + '/apps' })
await sleep(6000)
for (let i = 0; i < 20; i++) {
  const n = await evaluate(`document.querySelectorAll('.el-table__row').length`)
  if (n > 0) break
  await sleep(1000)
}
const info = await evaluate(`({
  url: location.href,
  rows: document.querySelectorAll('.el-table__row').length,
  text: document.body.innerText.slice(0, 300),
})`)
console.log('[page]', JSON.stringify(info))

if (EXPAND) {
  await evaluate(`document.querySelectorAll('.el-table__expand-icon').forEach(e => e.click()); 'ok'`)
  await sleep(3000)
}

if (CLICK_NODES_APP) {
  const clicked = await evaluate(`(() => {
    const rows = [...document.querySelectorAll('.el-table__row')]
    const row = rows.find(r => r.innerText.includes('${CLICK_NODES_APP}'))
    if (!row) return 'row-not-found'
    const btn = [...row.querySelectorAll('button')].find(b => /个节点/.test(b.innerText))
    if (!btn) return 'btn-not-found'
    btn.click()
    return 'clicked'
  })()`)
  console.log('[click-nodes]', clicked)
  await sleep(2500)
  if (CLICK_PAGE_2) {
    await evaluate(`(() => {
      const dlg = [...document.querySelectorAll('.el-dialog')].find(d => d.innerText.includes('的节'))
      const next = dlg?.querySelector('.el-pagination .btn-next')
      if (next) next.click()
      return 'next'
    })()`)
    await sleep(1500)
  }
  const dlgInfo = await evaluate(`(() => {
    const dlg = [...document.querySelectorAll('.el-dialog')].find(d => d.innerText.includes('的节'))
    if (!dlg) return 'dialog-not-found'
    return {
      rows: dlg.querySelectorAll('.el-table__row').length,
      pages: [...dlg.querySelectorAll('.el-pager li')].map(e => e.innerText).join(','),
      total: dlg.querySelector('.el-pagination__total')?.innerText || '',
    }
  })()`)
  console.log('[dialog]', JSON.stringify(dlgInfo))
}

if (CLICK_START_APP) {
  const clicked = await evaluate(`(() => {
    const rows = [...document.querySelectorAll('.el-table__row')]
    const row = rows.find(r => r.innerText.includes('${CLICK_START_APP}'))
    if (!row) return 'row-not-found'
    const btn = [...row.querySelectorAll('button')].find(b => b.innerText.trim() === '启动')
    if (!btn) return 'btn-not-found'
    btn.click()
    return 'clicked'
  })()`)
  console.log('[click]', clicked)
  await sleep(3000)
  const toast = await evaluate(`[...document.querySelectorAll('.el-message')].map(e => e.innerText).join(' | ')`)
  console.log('[toast]', toast)
}

const shot = await cmd('Page.captureScreenshot', { format: 'png' })
fs.writeFileSync(OUT, Buffer.from(shot.data, 'base64'))
console.log('[shot]', OUT)
process.exit(0)
