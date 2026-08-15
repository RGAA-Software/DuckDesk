// CDP 无头 Chrome 验证 gr_cms 多画面墙页面
// 用法: node scripts/cdp_video_wall_test.mjs
// 前提: px_cms_server 已在 https://127.0.0.1:30500 运行(部署了最新 dist)
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE_URL = 'https://127.0.0.1:30500'
const CDP_PORT = 9223
const USERNAME = 'SpvrAdmin'
const PASSWORD = 'eb#6naIq'

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
    '--ignore-certificate-errors',
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

// 给 el-input 内部 input 赋值并触发 Vue 更新
const SET_INPUT = (placeholder, value) => `(() => {
  const el = [...document.querySelectorAll('input')].find((e) => e.placeholder === ${JSON.stringify(placeholder)})
  if (!el) return 'NOT_FOUND'
  const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set
  setter.call(el, ${JSON.stringify(value)})
  el.dispatchEvent(new Event('input', { bubbles: true }))
  return 'OK'
})()`

const CLICK_BTN = (text) => `(() => {
  const el = [...document.querySelectorAll('button')].find((e) => e.textContent.trim() === ${JSON.stringify(text)})
  if (!el) return 'NOT_FOUND'
  el.click()
  return 'OK'
})()`

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

  // 1. 登录页加载并登录
  let loginReady = false
  for (let i = 0; i < 40; i++) {
    const ok = await evaluate(`[...document.querySelectorAll('input')].some((e) => e.placeholder === '请输入密码')`).catch(() => false)
    if (ok) { loginReady = true; break }
    await sleep(500)
  }
  report('登录页打开', loginReady)

  await evaluate(SET_INPUT('请输入', USERNAME))
  await evaluate(SET_INPUT('请输入密码', PASSWORD))
  await evaluate(CLICK_BTN('登录'))
  let loggedIn = false
  for (let i = 0; i < 30; i++) {
    const url = await evaluate('location.pathname').catch(() => '')
    if (url && url !== '/') { loggedIn = true; break }
    await sleep(500)
  }
  report('登录成功跳转', loggedIn, await evaluate('location.pathname').catch(() => ''))

  // 2. 进多画面墙页面(侧栏菜单)
  await evaluate(`location.href = '${BASE_URL}/video-wall'`)
  let pageReady = false
  for (let i = 0; i < 40; i++) {
    const ok = await evaluate(`!!document.querySelector('.el-select__wrapper') && [...document.querySelectorAll('span')].some((s) => s.textContent.includes('多画面墙'))`).catch(() => false)
    if (ok) { pageReady = true; break }
    await sleep(500)
  }
  report('多画面墙页面打开', pageReady)

  const menuOk = await evaluate(`[...document.querySelectorAll('.el-menu-item')].some((e) => e.textContent.includes('多画面墙'))`)
  report('侧栏菜单含「多画面墙」入口', menuOk)

  // 3. 打开设备下拉,等待设备列表加载
  await evaluate(`document.querySelector('.el-select__wrapper').click()`)
  let optionCount = 0
  for (let i = 0; i < 30; i++) {
    optionCount = await evaluate(`document.querySelectorAll('li.el-select-dropdown__item').length`).catch(() => 0)
    if (optionCount > 0) break
    await sleep(500)
  }
  report('设备列表加载', optionCount > 0, `可选设备 ${optionCount} 台`)
  if (optionCount === 0) throw new Error('无设备可选,中止')

  // 4. 勾选第 1 台可选(在线)设备 -> 出现格子 + iframe
  const picked = await evaluate(`(() => {
    const li = [...document.querySelectorAll('li.el-select-dropdown__item')].find((e) => !e.className.includes('is-disabled'))
    if (!li) return ''
    li.click()
    return li.textContent.trim()
  })()`)
  report('有可勾选(在线)设备', !!picked, picked)
  await sleep(1500)
  const cellCount1 = await evaluate(`document.querySelectorAll('.wall-cell').length`)
  report('勾选 1 台后出现格子', cellCount1 === 1, `格子数 ${cellCount1}`)

  const src1 = await evaluate(`document.querySelector('.wall-cell iframe')?.src ?? ''`)
  const srcRe = /^http:\/\/[\d.]+:\d+\/web_client\/\?deviceId=[^&]+&streamId=wall-[^&]+$/
  report('iframe src 拼接正确(无密码)', srcRe.test(src1), src1)

  // streamId 与 deviceId 对应关系: streamId=wall-{did}
  const pairOk = await evaluate(`(() => {
    const u = new URL(document.querySelector('.wall-cell iframe').src)
    return u.searchParams.get('streamId') === 'wall-' + u.searchParams.get('deviceId')
  })()`)
  report('streamId = wall-{deviceId}', pairOk === true)

  // 5. 统一密码 -> 应用到全部 -> iframe src 带 password 参数
  const TEST_PWD = 'Wall#Test123'
  await evaluate(SET_INPUT('应用到所有格子', TEST_PWD))
  await evaluate(CLICK_BTN('应用到全部'))
  await sleep(1500)
  const src2 = await evaluate(`document.querySelector('.wall-cell iframe')?.src ?? ''`)
  report('统一密码填充进 iframe src', src2.includes(`password=${encodeURIComponent(TEST_PWD)}`), src2)

  // 6. 格子内独立密码 + 连接按钮
  const CELL_PWD = 'cell only'
  await evaluate(SET_INPUT('安全密码', CELL_PWD))
  await evaluate(`(() => {
    const btn = [...document.querySelectorAll('.wall-cell button')].find((e) => e.textContent.trim() === '连接')
    if (!btn) return 'NOT_FOUND'
    btn.click()
    return 'OK'
  })()`)
  await sleep(1500)
  const src3 = await evaluate(`document.querySelector('.wall-cell iframe')?.src ?? ''`)
  report('格子独立密码填充进 iframe src', src3.includes(`password=${encodeURIComponent(CELL_PWD)}`), src3)

  // 7. 连接状态提示存在(已加载 或 10s 超时后无法到达 render)
  await sleep(11000)
  const statusText = await evaluate(`document.querySelector('.wall-cell .el-tag')?.textContent?.trim() ?? ''`)
  report('格子状态提示有效', statusText === '页面已加载' || statusText === '无法到达 render', statusText)

  const failed = results.filter((x) => !x.ok)
  console.log(`\n==== ${results.length - failed.length}/${results.length} 项通过 ====`)
  if (failed.length > 0) process.exitCode = 1
}

main()
  .catch((e) => { console.error('测试脚本异常:', e.message); process.exitCode = 1 })
  .finally(() => { try { chrome.kill() } catch { /* ignore */ } })
