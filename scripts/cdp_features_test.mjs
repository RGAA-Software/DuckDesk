// CDP 无头 Chrome 端到端验证 px_web_client 三个新功能:
//   1. 性能面板(getStats 采样) + 帧率画质档
//   2. 剪贴板文本同步(web<->render 系统剪贴板,经 px_user_proxy)
//   3. 触屏手势(Input.dispatchTouchEvent 注入,解码 px.Message 验证 + 物理光标)
// 用法: node scripts/cdp_features_test.mjs
// 依赖: 无(Node 22 内置 fetch/WebSocket),Chrome + GammaRay 套件已在运行
import { spawn, execFileSync } from 'node:child_process'
import { createRequire } from 'node:module'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=feat1&pwd_md5=698d51a19d8a121ce581499d7b701668'
const CDP_PORT = 9223

// ---------- px.Message 解码(复用 web 端 proto)----------
const require = createRequire(import.meta.url)
const protobuf = require('../web/px_web_client/node_modules/protobufjs')
const protoRoot = new protobuf.Root()
const protoDir = path.join(import.meta.dirname, '../web/px_web_client/proto')
for (const f of ['px_signaling_message.proto']) {
  protobuf.parse(fs.readFileSync(path.join(protoDir, f), 'utf8'), protoRoot)
}
const pxSrc = fs
  .readFileSync(path.join(protoDir, 'px_message.proto'), 'utf8')
  .replace(/^\s*import\s+"[^"]+"\s*;\s*$/gm, '')
protobuf.parse(pxSrc, protoRoot)
const PxMessage = protoRoot.lookupType('px.Message')

const MSG_MOUSE_EVENT = 60
const BTN = { LEFT_UP: 16, MIDDLE_UP: 32, RIGHT_UP: 64, MOVE: 128, WHEEL: 256, LEFT_DOWN: 1024, MIDDLE_DOWN: 2048, RIGHT_DOWN: 4096 }

function decodeSentMessages(b64List) {
  const out = []
  for (const b64 of b64List) {
    const buf = Buffer.from(b64, 'base64')
    if (buf.length < 32) continue // TLV 头 32 字节
    try {
      out.push(PxMessage.decode(buf.subarray(32)))
    } catch { /* 非 px.Message,忽略 */ }
  }
  return out
}

// ---------- PowerShell 助手 ----------
function ps(cmdStr) {
  return execFileSync('powershell', ['-NoProfile', '-Command', `[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; ${cmdStr}`], { encoding: 'utf8' }).trim()
}
const getSysClipboard = () => ps('Get-Clipboard | Out-String').trim()
const setSysClipboard = (text) => ps(`Set-Clipboard -Value '${text.replace(/'/g, "''")}'`)
const getCursorPos = () => {
  const s = ps('Add-Type -AssemblyName System.Windows.Forms; $p=[System.Windows.Forms.Cursor]::Position; "$($p.X),$($p.Y)"')
  const [x, y] = s.split(',').map(Number)
  return { x, y }
}

const results = []
function report(name, ok, detail = '') {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`)
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

// ---------- Chrome 启动 ----------
const profile = path.join(os.tmpdir(), `cdp-feat-${Date.now()}`)
const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--autoplay-policy=no-user-gesture-required',
    '--touch-events=enabled',
    'about:blank',
  ],
  { stdio: 'ignore' },
)
process.on('exit', () => {
  try { chrome.kill() } catch { /* ignore */ }
})

async function waitDevtools() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (r.ok) return
    } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools 端口未就绪')
}

// ---------- CDP 会话 ----------
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
  if (r.exceptionDetails) throw new Error('evaluate 异常: ' + JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}

const CLICK_JS = (text, scope = '.float-toolbar') => `(() => {
  const els = [...document.querySelectorAll('${scope} button, ${scope} .el-checkbox')]
  const el = els.find((e) => e.textContent.trim().includes(${JSON.stringify(text)}))
  if (!el) return 'NOT_FOUND'
  el.click()
  return 'OK'
})()`

// 触屏注入
async function touch(type, points) {
  await cmd('Input.dispatchTouchEvent', {
    type,
    touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: p.id ?? i + 1 })),
  })
}

// 拉取并清空页面记录的发送包,解码出鼠标事件
async function drainMouseEvents() {
  const sent = await evaluate('window.__sent ? window.__sent.splice(0) : []')
  return decodeSentMessages(sent)
    .filter((m) => m.type === MSG_MOUSE_EVENT && m.mouseEvent)
    .map((m) => ({
      button: m.mouseEvent.button,
      data: m.mouseEvent.data ?? 0,
      x: m.mouseEvent.xRatio,
      y: m.mouseEvent.yRatio,
    }))
}

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

  // 等待 WebRTC 自动连接
  let connected = false
  for (let i = 0; i < 80; i++) {
    const tag = await evaluate(`document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? ''`).catch(() => '')
    if (tag === '已连接') { connected = true; break }
    await sleep(500)
  }
  report('WebRTC 自动连接', connected)
  if (!connected) throw new Error('连接失败,中止')

  // 记录 datachannel 发送包(供触屏/剪贴板断言)
  await evaluate(`(() => {
    window.__sent = []
    if (window.__sendPatched) return
    window.__sendPatched = true
    const orig = RTCDataChannel.prototype.send
    RTCDataChannel.prototype.send = function (data) {
      try {
        if (data instanceof ArrayBuffer && window.__sent.length < 3000) {
          const u8 = new Uint8Array(data)
          let bin = ''
          for (let i = 0; i < u8.length; i++) bin += String.fromCharCode(u8[i])
          window.__sent.push(btoa(bin))
        }
      } catch (e) {}
      return orig.call(this, data)
    }
  })()`)

  // 展开工具条
  await evaluate(`document.querySelector('.float-toolbar .fab').click()`)
  await sleep(500)

  // ==================== 功能 1: 性能面板 ====================
  report('点击「性能」展开面板', (await evaluate(CLICK_JS('性能'))) === 'OK')
  await sleep(5500) // 等 >=2 个采样周期(2s/次)
  const perf = await evaluate('window.__perf ? window.__perf() : null')
  console.log('perf raw:', JSON.stringify(perf))
  const domBitrate = await evaluate(`document.querySelector('.perf-panel .perf-bitrate')?.textContent?.trim() ?? ''`)
  const domRes = await evaluate(`document.querySelector('.perf-panel .perf-resolution')?.textContent?.trim() ?? ''`)
  const domFps = await evaluate(`document.querySelector('.perf-panel .perf-fps')?.textContent?.trim() ?? ''`)
  report('性能面板数据(码率/帧率/分辨率/RTT)',
    !!perf && Number(perf.videoBitrateKbps) > 0 && Number(perf.fps) > 0 && Number(perf.width) > 0 && Number(perf.rttMs) >= 0,
    perf ? `码率=${Number(perf.videoBitrateKbps).toFixed(0)}kbps fps=${Number(perf.fps).toFixed(1)} RTT=${Number(perf.rttMs).toFixed(1)}ms 丢包=${(Number(perf.lossRate) * 100).toFixed(2)}% 抖动=${Number(perf.jitterMs).toFixed(1)}ms 分辨率=${perf.width}x${perf.height}` : 'no __perf')
  report('性能面板 DOM 渲染', /\d/.test(domBitrate) && /\d/.test(domFps) && /×/.test(domRes),
    `码率="${domBitrate}" 帧率="${domFps}" 分辨率="${domRes}"`)

  // ==================== 功能 2: 剪贴板文本同步 ====================
  const hasSendBtn = (await evaluate(CLICK_JS('发送到远端'))) !== 'NOT_FOUND' // 注意:这会真的点击
  await sleep(300)
  report('剪贴板按钮存在(发送到远端)', hasSendBtn)

  // 2a. web -> render 系统剪贴板(经 px_user_proxy 写入)
  const w2r = `W2R-${Date.now()}-剪贴板测试`
  const sent = await evaluate(`window.__clipboard.sendText(${JSON.stringify(w2r)})`)
  report('调用 __clipboard.sendText', sent === true)
  await sleep(2500)
  const sysClip = getSysClipboard()
  report('远端系统剪贴板已写入(web->remote)', sysClip.includes(w2r), `Get-Clipboard="${sysClip.slice(0, 60)}"`)

  // 2b. render 系统剪贴板 -> web(px_user_proxy 监听 -> kClipboardInfo 广播)
  const r2w = `R2W-${Date.now()}-远端文本`
  setSysClipboard(r2w)
  let lastRemote = ''
  for (let i = 0; i < 14; i++) {
    lastRemote = await evaluate('window.__clipboard.lastRemote()')
    if (lastRemote.includes(r2w)) break
    await sleep(500)
  }
  report('web 收到远端剪贴板(remote->web)', lastRemote.includes(r2w), `lastRemote="${String(lastRemote).slice(0, 60)}"`)
  const copyBtn = await evaluate(`!![...document.querySelectorAll('.float-toolbar button')].find(b => b.textContent.includes('复制到本地'))`)
  report('「复制到本地」按钮出现(有远端内容后)', copyBtn === true)

  // ==================== 功能 3: 触屏手势 ====================
  const vrect = await evaluate(`(() => { const r = document.querySelector('video.remote-video').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height } })()`)
  const cx = Math.round(vrect.x + vrect.w / 2)
  const cy = Math.round(vrect.y + vrect.h / 2)
  await drainMouseEvents() // 清空

  // 3a. 单指 tap = 左键
  await touch('touchStart', [{ x: cx, y: cy }])
  await sleep(80)
  await touch('touchEnd', [])
  await sleep(400)
  let evs = await drainMouseEvents()
  const tapDown = evs.find((e) => e.button === BTN.LEFT_DOWN)
  const tapUp = evs.find((e) => e.button === BTN.LEFT_UP)
  report('单指 tap -> 左键点击', !!tapDown && !!tapUp && Math.abs(tapDown.x - 0.5) < 0.02 && Math.abs(tapDown.y - 0.5) < 0.02,
    tapDown ? `down@(${tapDown.x.toFixed(3)},${tapDown.y.toFixed(3)}) up=${!!tapUp}` : `无 LEFT_DOWN,收到 ${evs.length} 条`)

  // 3b. 单指拖动 = 鼠标移动(同时看物理光标;先把光标挪到已知位置,避免与上次拖拽终点重合)
  ps('Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(60,60)')
  await sleep(200)
  const cursorBefore = getCursorPos()
  await touch('touchStart', [{ x: cx, y: cy }])
  for (let i = 1; i <= 6; i++) {
    await touch('touchMove', [{ x: cx + i * 20, y: cy - i * 10 }])
    await sleep(50)
  }
  await touch('touchEnd', [])
  await sleep(600)
  evs = await drainMouseEvents()
  const moves = evs.filter((e) => e.button === BTN.MOVE)
  const cursorAfter = getCursorPos()
  const cursorMoved = Math.abs(cursorAfter.x - cursorBefore.x) + Math.abs(cursorAfter.y - cursorBefore.y) > 20
  report('单指拖动 -> 鼠标移动消息', moves.length >= 2 && moves[moves.length - 1].x > moves[0].x,
    `move 消息 ${moves.length} 条, xRatio ${moves[0]?.x.toFixed(3)} -> ${moves[moves.length - 1]?.x.toFixed(3)}`)
  report('单指拖动 -> 物理光标移动(render SendInput)', cursorMoved,
    `(${cursorBefore.x},${cursorBefore.y}) -> (${cursorAfter.x},${cursorAfter.y})`)

  // 3c. 单指长按 500ms = 右键
  await touch('touchStart', [{ x: cx, y: cy }])
  await sleep(750)
  await touch('touchEnd', [])
  await sleep(400)
  evs = await drainMouseEvents()
  const rDown = evs.find((e) => e.button === BTN.RIGHT_DOWN)
  const rUp = evs.find((e) => e.button === BTN.RIGHT_UP)
  report('单指长按 -> 右键', !!rDown && !!rUp, `RIGHT_DOWN=${!!rDown} RIGHT_UP=${!!rUp}`)

  // 3d. 双指拖动 = 滚轮
  await touch('touchStart', [{ x: cx - 30, y: cy, id: 1 }, { x: cx + 30, y: cy, id: 2 }])
  for (let i = 1; i <= 5; i++) {
    await touch('touchMove', [{ x: cx - 30, y: cy + i * 30, id: 1 }, { x: cx + 30, y: cy + i * 30, id: 2 }])
    await sleep(50)
  }
  await touch('touchEnd', [])
  await sleep(400)
  evs = await drainMouseEvents()
  const wheels = evs.filter((e) => e.button === BTN.WHEEL)
  report('双指拖动 -> 滚轮消息', wheels.length >= 1 && wheels.every((w) => w.data !== 0),
    `wheel ${wheels.length} 条, data=${wheels.map((w) => w.data).join(',')}`)

  // 3e. 双指 tap = 中键
  await touch('touchStart', [{ x: cx - 30, y: cy, id: 1 }, { x: cx + 30, y: cy, id: 2 }])
  await sleep(120)
  await touch('touchEnd', [])
  await sleep(400)
  evs = await drainMouseEvents()
  const mDown = evs.find((e) => e.button === BTN.MIDDLE_DOWN)
  const mUp = evs.find((e) => e.button === BTN.MIDDLE_UP)
  report('双指 tap -> 中键', !!mDown && !!mUp, `MIDDLE_DOWN=${!!mDown} MIDDLE_UP=${!!mUp}`)

  // ==================== 画质档(协议无改码率消息,帧率档调节 + 面板看自适应码率)====================
  // 放最后:kModifyFps 曾观察到 render 编码缓存竞态崩溃风险
  await evaluate(CLICK_JS('30', '.float-toolbar .fps-group'))
  await sleep(6000)
  const perf2 = await evaluate('window.__perf ? window.__perf() : null').catch(() => null)
  report('画质档(帧率 30)生效', !!perf2 && Number(perf2.fps) > 3 && Number(perf2.fps) <= 45,
    perf2 ? `fps=${Number(perf2.fps).toFixed(1)} 码率=${Number(perf2.videoBitrateKbps).toFixed(0)}kbps(自适应)` : '采样失败(render 可能已重启)')

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 项通过 ====`)
  if (failed.length) process.exitCode = 1
}

main()
  .catch((e) => { console.error('测试脚本异常:', e.message); process.exitCode = 1 })
  .finally(() => { try { chrome.kill() } catch { /* ignore */ } })
