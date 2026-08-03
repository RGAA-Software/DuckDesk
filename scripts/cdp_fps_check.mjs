// 专项验证:kModifyFps(30) 是否被 render 应用
// 通过 window.__pc.getStats() 的 inbound-rtp framesPerSecond 判定(无头下 totalVideoFrames 不可靠)
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=toolbar1&pwd_md5=698d51a19d8a121ce581499d7b701668'
const CDP_PORT = 9223

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const profile = path.join(os.tmpdir(), `cdp-fps-${Date.now()}`)
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`, '--no-first-run', 'about:blank'], { stdio: 'ignore' })
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
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}

async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try { const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`); if (r.ok) return } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools 未就绪')
}

const FPS_JS = `(async () => {
  const pc = window.__pc
  if (!pc) return { err: 'no __pc' }
  const stats = await pc.getStats()
  const out = []
  stats.forEach((s) => {
    if (s.type === 'inbound-rtp' && s.kind === 'video') {
      out.push({ fps: s.framesPerSecond, framesReceived: s.framesReceived, framesDecoded: s.framesDecoded, state: pc.connectionState })
    }
  })
  return out
})()`

async function main() {
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(PAGE_URL)}`, { method: 'PUT' })
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

  let connected = false
  for (let i = 0; i < 80; i++) {
    const tag = await evaluate(`document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? ''`).catch(() => '')
    if (tag === '已连接') { connected = true; break }
    await sleep(500)
  }
  console.log('connected:', connected)
  if (!connected) throw new Error('未连接')

  await sleep(3000) // 等码流稳定
  const before1 = await evaluate(FPS_JS)
  await sleep(2000)
  const before2 = await evaluate(FPS_JS)
  console.log('fps before:', JSON.stringify(before1), JSON.stringify(before2))

  // 展开工具条并点「30」
  await evaluate(`document.querySelector('.float-toolbar .fab').click()`)
  await sleep(500)
  const clicked = await evaluate(`(() => {
    const b = [...document.querySelectorAll('.float-toolbar .fps-group button')].find((x) => x.textContent.trim() === '30')
    if (!b) return 'NOT_FOUND'
    b.click()
    return 'OK'
  })()`)
  console.log('click 30:', clicked)

  await sleep(6000) // 等采集端切换
  const after1 = await evaluate(FPS_JS)
  await sleep(2000)
  const after2 = await evaluate(FPS_JS)
  console.log('fps after :', JSON.stringify(after1), JSON.stringify(after2))
}

main()
  .catch((e) => { console.error('异常:', e.message); process.exitCode = 1 })
  .finally(() => {
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
    setTimeout(() => process.exit(process.exitCode ?? 0), 500).unref()
  })
