// CDP 无头 Chrome 验证 px_web_client 悬浮工具条
// 用法: node scripts/cdp_toolbar_test.mjs
// 依赖: 无(Node 22 内置 fetch/WebSocket),Chrome + Pixels 套件已在运行
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const RENDER_LOG = 'C:/Users/Public/Pixels/px_logs/pixels_render_20371.log'
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=toolbar1&pwd_md5=698d51a19d8a121ce581499d7b701668'
const CDP_PORT = 9222

const results = []
function report(name, ok, detail = '') {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`)
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

// ---------- Chrome 启动 ----------
const profile = path.join(os.tmpdir(), `cdp-toolbar-${Date.now()}`)
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
  // userGesture: 模拟用户激活,否则 requestFullscreen 等 API 被拒
  const r = await cmd('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true })
  if (r.exceptionDetails) throw new Error('evaluate 异常: ' + JSON.stringify(r.exceptionDetails).slice(0, 300))
  return r.result?.value
}

// 点击工具条面板内包含指定文字的按钮/复选框
const CLICK_JS = (text, scope = '.float-toolbar') => `(() => {
  const els = [...document.querySelectorAll('${scope} button, ${scope} .el-checkbox')]
  const el = els.find((e) => e.textContent.trim().includes(${JSON.stringify(text)}))
  if (!el) return 'NOT_FOUND'
  el.click()
  return 'OK'
})()`

function readNewLog(fromOffset) {
  const fd = fs.openSync(RENDER_LOG, 'r')
  try {
    const size = fs.fstatSync(fd).size
    if (size <= fromOffset) return ''
    const buf = Buffer.alloc(size - fromOffset)
    fs.readSync(fd, buf, 0, buf.length, fromOffset)
    return buf.toString('utf8')
  } finally {
    fs.closeSync(fd)
  }
}

async function getVideoFps() {
  // 无头模式下 requestVideoFrameCallback 不一定触发,改用解码帧计数差值估算
  return evaluate(`(async () => {
    const v = document.querySelector('video.remote-video')
    if (!v || !v.getVideoPlaybackQuality) return -1
    const q1 = v.getVideoPlaybackQuality().totalVideoFrames
    await new Promise((r) => setTimeout(r, 2000))
    return (v.getVideoPlaybackQuality().totalVideoFrames - q1) / 2
  })()`)
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

  // 等待 WebRTC 连接建立
  let connected = false
  for (let i = 0; i < 80; i++) {
    const tag = await evaluate(`document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? ''`).catch(() => '')
    if (tag === '已连接') { connected = true; break }
    await sleep(500)
  }
  report('WebRTC 自动连接', connected, connected ? '' : '状态标签未到「已连接」')
  if (!connected) {
    const logs = await evaluate(`document.querySelector('.log-panel')?.textContent?.slice(-800) ?? ''`).catch(() => '')
    console.log('页面日志尾部:', logs)
    throw new Error('连接失败,中止')
  }

  // 展开工具条
  const fab = await evaluate(`(() => { const b = document.querySelector('.float-toolbar .fab'); if (!b) return 'NOT_FOUND'; b.click(); return 'OK' })()`)
  await sleep(500)
  const panelVisible = await evaluate(`!!document.querySelector('.float-toolbar .panel')?.offsetParent`)
  report('工具条展开', fab === 'OK' && panelVisible)

  // 1. 刷新画面 -> render 日志应出现 "UpdateDesktop ..."
  let off = fs.statSync(RENDER_LOG).size
  report('点击「刷新画面」', (await evaluate(CLICK_JS('刷新画面'))) === 'OK')
  await sleep(2500)
  const hardUpd = readNewLog(off)
  report('render 收到 kHardUpdateDesktop', hardUpd.includes('UpdateDesktop'),
    hardUpd.split('\n').filter((l) => l.includes('UpdateDesktop')).slice(0, 2).join(' | ').trim())

  // 2. 声音开关
  const mutedBefore = await evaluate(`document.querySelector('video.remote-video').muted`)
  await evaluate(CLICK_JS(mutedBefore ? '取消静音' : '静音'))
  await sleep(300)
  const mutedAfter = await evaluate(`document.querySelector('video.remote-video').muted`)
  report('声音开关', mutedBefore === true && mutedAfter === false, `muted: ${mutedBefore} -> ${mutedAfter}`)

  // 3. 全屏切换
  await evaluate(CLICK_JS('全屏'))
  await sleep(600)
  const fsOn = await evaluate(`!!document.fullscreenElement`)
  await evaluate(CLICK_JS('退出全屏'))
  await sleep(600)
  const fsOff = await evaluate(`!document.fullscreenElement`)
  report('全屏切换', fsOn && fsOff, `enter=${fsOn} exit=${fsOff}`)

  // 4. 仅观看开关(前端状态)
  const voBefore = await evaluate(`document.querySelector('.float-toolbar .view-only-checkbox input').checked`)
  await evaluate(CLICK_JS('仅观看'))
  await sleep(300)
  const voAfter = await evaluate(`document.querySelector('.float-toolbar .view-only-checkbox input').checked`)
  report('仅观看开关', voBefore === false && voAfter === true, `checked: ${voBefore} -> ${voAfter}`)

  // 5. 锁屏 -> render 日志应出现 "Panel request LockScreen"(会真的锁本机;放在 fps 之前,
  //    因为 kModifyFps 可能触发 render 既有的编码帧缓存竞态崩溃,见汇报)
  off = fs.statSync(RENDER_LOG).size
  report('点击「锁屏」', (await evaluate(CLICK_JS('锁屏'))) === 'OK')
  await sleep(3000)
  const lockLog = readNewLog(off)
  report('render 收到 kLockDevice', lockLog.includes('Panel request LockScreen'),
    lockLog.split('\n').filter((l) => l.includes('LockScreen')).slice(0, 2).join(' | ').trim())

  // 6. 改帧率 30 -> render 无日志(见汇报),用画面帧率验证;放最后,容忍 render 崩溃
  const fpsBefore = await getVideoFps()
  report('点击「帧率 30」', (await evaluate(CLICK_JS('30', '.float-toolbar .fps-group'))) === 'OK')
  await sleep(4000) // 等采集端切换生效
  const fpsAfter = await getVideoFps()
  const fpsActive = await evaluate(`[...document.querySelectorAll('.float-toolbar .fps-group button')].find(b => b.textContent.trim() === '30')?.className.includes('el-button--primary') ?? false`)
  report('改帧率生效(画面帧率降至 ~30)', fpsAfter > 5 && fpsAfter < 45 && fpsAfter <= fpsBefore,
    `before=${fpsBefore.toFixed(1)} after=${fpsAfter.toFixed(1)} 按钮高亮=${fpsActive}`)

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 项通过 ====`)
}

main()
  .catch((e) => { console.error('测试脚本异常:', e.message); process.exitCode = 1 })
  .finally(() => { try { chrome.kill() } catch { /* ignore */ } })
