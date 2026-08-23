// Console web smoke (HTTP mode): login bypass via localStorage, visit key pages,
// verify rendering + zero console errors. Usage: node scripts/cdp_console_smoke.mjs
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE = 'http://10.0.0.16:30500'
const APPKEY = '49727717a74720a863f007dcdb13324e'
const CDP_PORT = 9495
const sleep = (ms) => new Promise(r => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-smoke-${Date.now()}`)
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`, '--no-first-run', '--window-size=1600,900', 'about:blank'], { stdio: 'ignore' })
process.on('exit', () => { try { chrome.kill() } catch {} })
let msgId = 0; const pending = new Map(); let ws
function cmd(m, p = {}) { return new Promise((res, rej) => { const id = ++msgId; pending.set(id, { res, rej }); ws.send(JSON.stringify({ id, method: m, params: p })) }) }
async function evaluate(e) { const r = await cmd('Runtime.evaluate', { expression: e, returnByValue: true, awaitPromise: true, userGesture: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 400)); return r.result?.value }
for (let i = 0; i < 60; i++) { try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) break } catch {} await sleep(500) }
const r0 = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(BASE + '/')}`, { method: 'PUT' })
const target = await r0.json()
ws = new WebSocket(target.webSocketDebuggerUrl)
await new Promise(r => ws.onopen = r)
ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id).res(m.result); pending.delete(m.id) } }
const consoleErrors = []
await cmd('Runtime.enable')
ws.addEventListener('message', (ev) => { const m = JSON.parse(ev.data); if (m.method === 'Runtime.exceptionThrown' || (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error')) consoleErrors.push(JSON.stringify(m.params).slice(0, 200)) })
// login bypass: token presence is all the router guard checks
await evaluate(`localStorage.setItem('token','smoke');localStorage.setItem('appkey','${APPKEY}');'ok'`)

const pages = [
  { path: '/devices-list', expect: '001190520' },
  { path: '/video-wall', expect: '' },
  { path: '/resources', expect: '' },
]
let fail = 0
for (const p of pages) {
  await cmd('Page.navigate', { url: `${BASE}${p.path}` })
  await sleep(4000)
  const text = await evaluate(`document.body.innerText`)
  const ok = p.expect ? text.includes(p.expect) : text.length > 50
  console.log(`PAGE ${p.path}: ${ok ? 'OK' : 'EMPTY/MISSING'} | ${text.replace(/\n/g, ' | ').slice(0, 300)}`)
  if (!ok) fail++
}
console.log('CONSOLE_ERRORS:', consoleErrors.length ? consoleErrors.join(' ;; ') : 'none')
if (consoleErrors.length) fail++
console.log(fail ? 'SMOKE_FAIL' : 'SMOKE_PASS')
process.exit(fail ? 1 : 0)
