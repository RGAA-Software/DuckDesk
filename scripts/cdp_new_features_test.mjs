// CDP 无头 Chrome 验证 px_web_client 新功能:画中画 / 本地录制 / 自动重连 / 指针锁定
// 用法: node scripts/cdp_new_features_test.mjs
// 依赖: 无(Node 22 内置 fetch/WebSocket),Chrome + GoDesk 套件已在运行
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const PAGE_URL =
  'http://127.0.0.1:20371/web_client/?deviceId=600378210&streamId=newfeat1&pwd_md5=698d51a19d8a121ce581499d7b701668'
const CDP_PORT = 9223

const results = []
function report(name, ok, detail = '') {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`)
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

const profile = path.join(os.tmpdir(), `cdp-newfeat-${Date.now()}`)
const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--autoplay-policy=no-user-gesture-required',
    '--window-size=1280,800',
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
  if (r.exceptionDetails) throw new Error('evaluate 异常: ' + JSON.stringify(r.exceptionDetails).slice(0, 400))
  return r.result?.value
}

async function waitConnStatus(want, timeoutMs) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const s = await evaluate(`window.__conn ? window.__conn.status() : ''`).catch(() => '')
    if (s === want) return true
    await sleep(500)
  }
  return false
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

  // 等 WebRTC 自动连接
  const connected = await waitConnStatus('connected', 40000)
  report('WebRTC 自动连接', connected)
  if (!connected) {
    const logs = await evaluate(`document.querySelector('.log-panel')?.textContent?.slice(-800) ?? ''`).catch(() => '')
    console.log('页面日志尾部:', logs)
    throw new Error('连接失败,中止')
  }
  // 等首帧且持续出帧(画中画/录制/坐标换算都依赖解码中的视频)
  const framesUp = await evaluate(`(async () => {
    const v = document.querySelector('video.remote-video')
    for (let i = 0; i < 30; i++) {
      if (v.videoWidth > 0 && v.readyState >= 2) {
        const q1 = v.getVideoPlaybackQuality().totalVideoFrames
        await new Promise((r) => setTimeout(r, 1000))
        if (v.getVideoPlaybackQuality().totalVideoFrames > q1) return true
      } else {
        await new Promise((r) => setTimeout(r, 1000))
      }
    }
    return false
  })()`)
  report('视频持续出帧', framesUp)
  if (!framesUp) throw new Error('视频无持续帧,中止')

  // ---------- 1. 画中画 ----------
  const pipSupported = await evaluate(`window.__pip.supported()`)
  if (pipSupported) {
    await evaluate(`window.__pip.toggle()`)
    await sleep(800)
    const pipEl = await evaluate(`!!document.pictureInPictureElement`)
    report('画中画进入', pipEl, `pictureInPictureElement=${pipEl}`)
    await evaluate(`window.__pip.toggle()`)
    await sleep(800)
    const pipOff = await evaluate(`!document.pictureInPictureElement`)
    report('画中画退出', pipOff)
  } else {
    report('画中画进入', false, '浏览器不支持 requestPictureInPicture')
  }

  // ---------- 2. 本地录制 ----------
  const recStarted = await evaluate(`window.__rec.start()`)
  report('录制启动', recStarted === true, `start()=${recStarted}`)
  await sleep(3000)
  const recState = await evaluate(`({ recording: window.__rec.recording(), seconds: window.__rec.seconds() })`)
  const recResult = await evaluate(`window.__rec.stop()`)
  report('录制 3s 后停止产出 webm', !!recResult && recResult.size > 0,
    `state=${JSON.stringify(recState)} result=${JSON.stringify(recResult)}`)

  // ---------- 3. 指针锁定(相对鼠标)----------
  // 无头下 requestPointerLock 需要 userGesture(evaluate 已带);锁定后注入合成 mousemove
  await evaluate(`(() => { const els = [...document.querySelectorAll('.float-toolbar .fab')]; els[0]?.click(); return 1 })()`)
  await sleep(400)
  await evaluate(`(() => {
    const btns = [...document.querySelectorAll('.float-toolbar button')]
    const b = btns.find((x) => x.textContent.trim().includes('锁定鼠标'))
    if (b) b.click()
    return !!b
  })()`)
  await sleep(800)
  const lockEl = await evaluate(`document.pointerLockElement?.className ?? ''`)
  const relOn = await evaluate(`window.__input.relative()`)
  report('指针锁定生效', lockEl.includes('remote-video') && relOn === true,
    `pointerLockElement=${lockEl} relativeMode=${relOn}`)
  // 合成相对位移:movementX=100, movementY=40
  await evaluate(`document.querySelector('video.remote-video').dispatchEvent(new MouseEvent('mousemove', { movementX: 100, movementY: 40 }))`)
  await sleep(300)
  const lastMouse = await evaluate(`window.__input.lastMouse()`)
  const virtPos = await evaluate(`window.__input.virtualPos()`)
  // 方案:回放侧不支持 delta,页内虚拟光标换算绝对坐标;xRatio 应从 0.5 右移,deltaX/Y 填原始位移
  const deltaOk = lastMouse && lastMouse.deltaX === 100 && lastMouse.deltaY === 40
  const absOk = lastMouse && lastMouse.xRatio > 0.5 && lastMouse.xRatio <= 1 && lastMouse.yRatio > 0.5
  const clampOk = virtPos && virtPos.x >= 0 && virtPos.x <= 1 && virtPos.y >= 0 && virtPos.y <= 1
  report('相对位移包字段正确(虚拟光标绝对坐标 + delta 字段)', !!(deltaOk && absOk && clampOk),
    `lastMouse=${JSON.stringify(lastMouse)} virt=${JSON.stringify(virtPos)}`)
  // 锁定状态 UI 提示
  const hintVisible = await evaluate(`!!document.querySelector('.lock-hint')`)
  report('锁定状态 UI 提示', hintVisible)
  // Esc 退出为浏览器原生行为,这里用 exitPointerLock 等效验证状态回路
  await evaluate(`document.exitPointerLock()`)
  await sleep(500)
  const relOff = await evaluate(`window.__input.relative()`)
  const lockOff = await evaluate(`!document.pointerLockElement`)
  report('解除指针锁定', lockOff && relOff === false, `relativeMode=${relOff}`)

  // ---------- 4. 自动重连 ----------
  await evaluate(`window.__pc.close()`)
  const reconnecting = await waitConnStatus('reconnecting', 5000)
  const retryN = await evaluate(`window.__conn.reconnectCount()`)
  report('强制断开后进入重连中', reconnecting && retryN >= 1, `reconnecting=${reconnecting} 第${retryN}次`)
  const tagText = await evaluate(`document.querySelector('.toolbar .el-tag')?.textContent?.trim() ?? ''`)
  report('状态栏显示「重连中(第 N 次)」', tagText.includes('重连中'), `tag="${tagText}"`)
  const reconnected = await waitConnStatus('connected', 40000)
  report('自动重连恢复 connected', reconnected)
  // 画面恢复:最多等 20s 让解码帧数重新增长
  const framesAdv = await evaluate(`(async () => {
    const v = document.querySelector('video.remote-video')
    if (!v || !v.srcObject) return -1
    for (let i = 0; i < 10; i++) {
      const q1 = v.getVideoPlaybackQuality().totalVideoFrames
      await new Promise((r) => setTimeout(r, 2000))
      const d = v.getVideoPlaybackQuality().totalVideoFrames - q1
      if (d > 0) return d
    }
    return 0
  })()`)
  report('重连后视频恢复出帧', framesAdv > 0, `采样窗口内新增 ${framesAdv} 帧`)
  // localStorage 预填记忆
  const lastConn = await evaluate(`localStorage.getItem('px_web_client.last_conn')`)
  report('localStorage 记忆 deviceId/streamId(不含密码)',
    !!lastConn && lastConn.includes('600378210') && !lastConn.includes('698d51a1'), lastConn ?? '')

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 项通过 ====`)
  if (failed.length) process.exitCode = 1
}

main()
  .catch((e) => { console.error('测试脚本异常:', e.message); process.exitCode = 1 })
  .finally(() => { try { chrome.kill() } catch { /* ignore */ } })
