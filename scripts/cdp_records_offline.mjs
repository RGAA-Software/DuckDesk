import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE = 'http://10.0.0.16:30500'
const APPKEY = '49727717a74720a863f007dcdb13324e'
const CDP_PORT = 9493
const sleep = (ms) => new Promise(r => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-off-${Date.now()}`)
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`, '--no-first-run', '--window-size=1600,900', 'about:blank'], { stdio: 'ignore' })
process.on('exit', () => { try { chrome.kill() } catch {} })
let msgId = 0; const pending = new Map(); let ws
function cmd(m, p = {}) { return new Promise((res, rej) => { const id = ++msgId; pending.set(id, { res, rej }); ws.send(JSON.stringify({ id, method: m, params: p })) }) }
async function evaluate(e) { const r = await cmd('Runtime.evaluate', { expression: e, returnByValue: true, awaitPromise: true, userGesture: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 400)); return r.result?.value }
for (let i = 0; i < 60; i++) { try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) break } catch {} await sleep(500) }
const r0 = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(BASE + '/')}`, { method: 'PUT' })
const target = await r0.json(); ws = new WebSocket(target.webSocketDebuggerUrl)
ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.rej(new Error(m.error.message)) : p.res(m.result) } }
await new Promise(res => { ws.onopen = res })
await cmd('Runtime.enable'); await cmd('Page.enable')
await evaluate(`localStorage.setItem('token','e2e');localStorage.setItem('appkey','${APPKEY}');'ok'`)
await cmd('Page.navigate', { url: `${BASE}/records/${process.env.DEVICE_ID || '001190520'}` })
await sleep(10000)
const body = await evaluate(`document.body.innerText.split('\\n').join(' | ').slice(0, 600)`)
console.log('PAGE:', body)
console.log(body.includes('设备离线') ? 'OFFLINE_MSG_OK' : 'OFFLINE_MSG_MISSING')
process.exit(0)
