// CDP 无头 Chrome 验证 px_cms 设备监控页面
// 用法: node scripts/cdp_video_wall_test.mjs
// 前提: px_cms_server 已在 https://127.0.0.1:30500 运行(部署了最新 dist)
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE_URL = process.env.PX_CMS_TEST_BASE_URL || 'https://127.0.0.1:30500'
const CDP_PORT = 9223
const USERNAME = 'CmsAdmin'
const PASSWORD = process.env.PX_CMS_TEST_PASSWORD || 'eb#6naIq'
const MANUAL_CERT_BYPASS = process.env.PX_CMS_TEST_MANUAL_CERT_BYPASS === '1'

const results = []
function report(name, ok, detail = '') {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`)
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

// ---------- Chrome 启动 ----------
const profile = path.join(os.tmpdir(), `cdp-videowall-${Date.now()}`)
const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    ...(MANUAL_CERT_BYPASS ? [] : ['--ignore-certificate-errors']),
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
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

async function main() {
  await waitDevtools()
  const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(BASE_URL + '/')}`, { method: 'PUT' })
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

  if (MANUAL_CERT_BYPASS) {
    for (let i = 0; i < 20; i++) {
      const bypassed = await evaluate(`(() => {
        const details = document.querySelector('#details-button')
        if (details) details.click()
        const proceed = document.querySelector('#proceed-link')
        if (proceed) { proceed.click(); return true }
        return false
      })()`).catch(() => false)
      if (bypassed) break
      await sleep(250)
    }
  }

  // 1. 登录页加载并通过同源 API 建立管理员会话。测试密码从环境变量注入，
  // 避免本地管理员改密后把明文凭据固化进仓库。
  let loginReady = false
  for (let i = 0; i < 40; i++) {
    const ok = await evaluate(`[...document.querySelectorAll('input')].some((e) => e.placeholder === '请输入密码')`).catch(() => false)
    if (ok) { loginReady = true; break }
    await sleep(500)
  }
  report('登录页打开', loginReady)

  const loginControls = await evaluate(`(() => {
    const inputs = [...document.querySelectorAll('input')]
    const usernameInput = inputs.find((input) => input.placeholder === '请输入')
    const passwordInput = inputs.find((input) => input.placeholder === '请输入密码')
    const loginButton = [...document.querySelectorAll('button')]
      .find((button) => button.textContent.replace(/\\s/g, '') === '登录')
    return {
      usernameInput: Boolean(usernameInput),
      passwordInput: Boolean(passwordInput),
      loginButton: Boolean(loginButton),
      buttonTexts: [...document.querySelectorAll('button')].map((button) => button.textContent.trim()),
    }
  })()`)
  report('登录表单控件完整', loginControls.usernameInput && loginControls.passwordInput && loginControls.loginButton, JSON.stringify(loginControls))
  if (!loginControls.usernameInput || !loginControls.passwordInput || !loginControls.loginButton) {
    throw new Error('登录表单控件不完整')
  }
  await evaluate(`(() => {
    const inputs = [...document.querySelectorAll('input')]
    const usernameInput = inputs.find((input) => input.placeholder === '请输入')
    const passwordInput = inputs.find((input) => input.placeholder === '请输入密码')
    const setValue = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set
    setValue.call(usernameInput, ${JSON.stringify(USERNAME)})
    usernameInput.dispatchEvent(new Event('input', { bubbles: true }))
    setValue.call(passwordInput, ${JSON.stringify(PASSWORD)})
    passwordInput.dispatchEvent(new Event('input', { bubbles: true }))
    const loginButton = [...document.querySelectorAll('button')]
      .find((button) => button.textContent.replace(/\\s/g, '') === '登录')
    loginButton.click()
  })()`)
  let loggedIn = false
  for (let i = 0; i < 40; i++) {
    loggedIn = await evaluate(`location.pathname !== '/'`).catch(() => false)
    if (loggedIn) break
    await sleep(250)
  }
  const loginResult = await evaluate(`fetch('/api/v1/session/admin/me', { credentials: 'same-origin' })
    .then(async (response) => ({ status: response.status, body: await response.json() }))`)
  loggedIn = loggedIn && loginResult?.status === 200 && loginResult?.body?.code === 200
  report('表单登录与管理员会话建立', loggedIn, JSON.stringify({
    path: await evaluate('location.pathname'),
    meStatus: loginResult?.status,
    meCode: loginResult?.body?.code,
  }))
  if (!loggedIn) throw new Error('管理员会话建立失败')

  const storedCookies = (await cmd('Storage.getCookies')).cookies
    .filter((cookie) => cookie.domain === new URL(BASE_URL).hostname)
    .map((cookie) => ({ name: cookie.name, secure: cookie.secure, path: cookie.path }))
  const adminCookieNames = storedCookies.map((cookie) => cookie.name)
  report(
    '管理员 Cookie 写入 Chrome',
    adminCookieNames.includes('__Host-px_admin_session') || adminCookieNames.includes('px_admin_session'),
    JSON.stringify(storedCookies),
  )

  // 2. 使用 CDP 的真实鼠标事件点击侧栏，验证页面没有透明遮罩或事件失效。
  const monitorMenu = await evaluate(`(() => {
    const item = [...document.querySelectorAll('.ant-menu-item')]
      .find((element) => element.textContent.includes('设备监控'))
    if (!item) return null
    const rect = item.getBoundingClientRect()
    const x = rect.left + rect.width / 2
    const y = rect.top + rect.height / 2
    const hit = document.elementFromPoint(x, y)
    return {
      x,
      y,
      hitText: hit?.textContent?.trim() || '',
      hitClass: hit?.className || '',
      itemContainsHit: Boolean(hit && item.contains(hit)),
    }
  })()`)
  report('设备监控菜单鼠标落点未被遮挡', Boolean(monitorMenu?.itemContainsHit), JSON.stringify(monitorMenu))
  if (!monitorMenu?.itemContainsHit) throw new Error('设备监控菜单被其他元素遮挡')
  await cmd('Input.dispatchMouseEvent', { type: 'mouseMoved', x: monitorMenu.x, y: monitorMenu.y })
  await cmd('Input.dispatchMouseEvent', { type: 'mousePressed', x: monitorMenu.x, y: monitorMenu.y, button: 'left', clickCount: 1 })
  await cmd('Input.dispatchMouseEvent', { type: 'mouseReleased', x: monitorMenu.x, y: monitorMenu.y, button: 'left', clickCount: 1 })
  let menuClickWorked = false
  for (let i = 0; i < 20; i++) {
    menuClickWorked = await evaluate(`location.pathname === '/video-wall'`).catch(() => false)
    if (menuClickWorked) break
    await sleep(250)
  }
  report('真实鼠标点击触发路由切换', menuClickWorked, await evaluate('location.pathname'))
  if (!menuClickWorked) throw new Error('真实鼠标点击未触发路由切换')

  // 3. 进设备监控页面(侧栏菜单)
  let pageReady = false
  for (let i = 0; i < 40; i++) {
    const ok = await evaluate(`[...document.querySelectorAll('h2')].some((e) => e.textContent.trim() === '设备监控')`).catch(() => false)
    if (ok) { pageReady = true; break }
    await sleep(500)
  }
  report('设备监控页面打开', pageReady)

  const menuOk = await evaluate(`[...document.querySelectorAll('.ant-menu-item')].some((e) => e.textContent.includes('设备监控'))`)
  report('侧栏菜单含「设备监控」入口', menuOk)

  // 3. 设备自动排序并直接建立只读 WebRTC 观察者连接，无人工选择、密码或 iframe。
  let cellCount = 0
  for (let i = 0; i < 40; i++) {
    cellCount = await evaluate(`document.querySelectorAll('.wall-cell').length`).catch(() => 0)
    if (cellCount > 0) break
    await sleep(500)
  }
  report('设备自动进入九宫格', cellCount > 0 && cellCount <= 9, `当前页 ${cellCount} 台`)
  if (cellCount === 0) throw new Error('没有设备进入监控网格')

  const mediaShape = await evaluate(`({
    videos: document.querySelectorAll('.wall-cell video').length,
    iframes: document.querySelectorAll('.wall-cell iframe').length,
    muted: [...document.querySelectorAll('.wall-cell video')].every((v) => v.muted),
  })`)
  report('每格使用原生静音 video', mediaShape.videos === cellCount && mediaShape.iframes === 0 && mediaShape.muted, JSON.stringify(mediaShape))

  let statusText = ''
  for (let i = 0; i < 60; i++) {
    statusText = await evaluate(`document.querySelector('.wall-cell .ant-tag')?.textContent?.trim() ?? ''`).catch(() => '')
    if (statusText === '画面传输中' || statusText === '连接失败') break
    await sleep(500)
  }
  report('观察者 WebRTC 收到画面', statusText === '画面传输中', statusText)

  const playback = await evaluate(`(() => {
    const video = document.querySelector('.wall-cell video')
    const footer = document.querySelector('.wall-cell .cell-footer')?.textContent?.trim() || ''
    return {
      readyState: video?.readyState || 0,
      width: video?.videoWidth || 0,
      height: video?.videoHeight || 0,
      frames: video?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
      footer,
    }
  })()`)
  report('视频已解码且显示链路有统计', playback.readyState >= 2 && playback.width > 0 && playback.height > 0 && playback.frames > 0, JSON.stringify(playback))

  // 保持超过首次连接超时阈值，防止已连接观察者被误当作建连超时清理。
  await sleep(20000)
  const stablePlayback = await evaluate(`(() => {
    const video = document.querySelector('.wall-cell video')
    return {
      status: document.querySelector('.wall-cell .ant-tag')?.textContent?.trim() || '',
      frames: video?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
    }
  })()`)
  report('观察者连接持续稳定', stablePlayback.status === '画面传输中' && stablePlayback.frames > playback.frames, JSON.stringify(stablePlayback))

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 项通过 ====`)
  if (failed.length > 0) process.exitCode = 1
}

main()
  .catch((e) => { console.error('测试脚本异常:', e.message); process.exitCode = 1 })
  .finally(() => { try { chrome.kill() } catch { /* ignore */ } })
