// Console 上报事件页面的浏览器回归测试。
// 用法: PX_CONSOLE_TEST_SESSION=<admin session token> node scripts/cdp_event_page_test.mjs
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE_URL = process.env.PX_CONSOLE_TEST_BASE_URL || process.env.PX_CMS_TEST_BASE_URL || 'https://127.0.0.1:30500'
const SESSION_TOKEN = process.env.PX_CONSOLE_TEST_SESSION || process.env.PX_CMS_TEST_SESSION || ''
const CDP_PORT = 9225
const SCREENSHOT = path.resolve('output/event-page-regression.png')
const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds))

if (!SESSION_TOKEN) throw new Error('PX_CONSOLE_TEST_SESSION is required')

const chrome = spawn(
  CHROME,
  [
    '--headless=new',
    '--ignore-certificate-errors',
    `--remote-debugging-port=${CDP_PORT}`,
    `--user-data-dir=${path.join(os.tmpdir(), `cdp-event-${Date.now()}`)}`,
    '--window-size=1600,900',
    '--no-first-run',
    'about:blank',
  ],
  { stdio: 'ignore' },
)
process.on('exit', () => {
  try { chrome.kill() } catch { /* already stopped */ }
})

let messageId = 0
let socket
const pending = new Map()
const browserErrors = []

function command(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++messageId
    pending.set(id, { resolve, reject })
    socket.send(JSON.stringify({ id, method, params }))
  })
}

async function evaluate(expression) {
  const response = await command('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  })
  if (response.exceptionDetails) throw new Error(JSON.stringify(response.exceptionDetails).slice(0, 500))
  return response.result?.value
}

function assert(name, condition, detail = '') {
  console.log(`${condition ? 'PASS' : 'FAIL'}  ${name}${detail ? ` -- ${detail}` : ''}`)
  if (!condition) throw new Error(name)
}

for (let attempt = 0; attempt < 60; attempt++) {
  try {
    const response = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
    if (response.ok) break
  } catch { /* wait for Chrome */ }
  await sleep(250)
}

try {
  const targetResponse = await fetch(
    `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(`${BASE_URL}/`)}`,
    { method: 'PUT' },
  )
  const target = await targetResponse.json()
  socket = new WebSocket(target.webSocketDebuggerUrl)
  await new Promise((resolve, reject) => {
    socket.onopen = resolve
    socket.onerror = reject
  })
  socket.onmessage = (event) => {
    const message = JSON.parse(event.data)
    if (message.id && pending.has(message.id)) {
      const request = pending.get(message.id)
      pending.delete(message.id)
      message.error ? request.reject(new Error(message.error.message)) : request.resolve(message.result)
      return
    }
    if (message.method === 'Runtime.exceptionThrown') browserErrors.push(JSON.stringify(message.params))
    if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
      browserErrors.push(JSON.stringify(message.params))
    }
  }

  await command('Runtime.enable')
  await command('Page.enable')
  await command('Network.enable')
  const cookie = await command('Network.setCookie', {
    name: '__Host-px_admin_session',
    value: SESSION_TOKEN,
    url: BASE_URL,
    path: '/',
    secure: true,
    httpOnly: true,
    sameSite: 'Lax',
  })
  assert('注入临时管理员会话', cookie.success === true)
  await command('Page.navigate', { url: `${BASE_URL}/events` })

  let ready = false
  for (let attempt = 0; attempt < 60; attempt++) {
    ready = await evaluate(`document.querySelector('.event-page h2')?.textContent?.trim() === '上报事件'`).catch(() => false)
    if (ready) break
    await sleep(250)
  }
  assert('事件页面打开', ready)

  const initial = await evaluate(`(() => ({
    summaries: document.querySelectorAll('.summary-card').length,
    headers: [...document.querySelectorAll('.ant-table-thead th')].map((node) => node.textContent.trim()),
    rows: document.querySelectorAll('.ant-table-tbody .ant-table-row').length,
    bodyOverflow: document.documentElement.scrollWidth - window.innerWidth,
    fontSize: getComputedStyle(document.querySelector('.ant-table-cell')).fontSize,
  }))()`)
  assert('四类统计卡片完整', initial.summaries === 4, JSON.stringify(initial))
  const expectedHeaders = ['时间', '类型', '资源', '使用率', '累计上报', '设备', '设备 IP', '上报用户', '事件 ID', '操作']
  assert('横向字段完整', expectedHeaders.every((header) => initial.headers.includes(header)), initial.headers.join(' / '))
  assert('页面不产生整页横向溢出', initial.bodyOverflow <= 2, `overflow=${initial.bodyOverflow}px`)
  assert('表格字号正常', initial.fontSize === '13px', initial.fontSize)

  await evaluate(`document.querySelectorAll('.summary-card')[2].click()`)
  await sleep(500)
  const disk = await evaluate(`(() => ({
    selected: document.querySelectorAll('.summary-card')[2].classList.contains('active'),
    rows: document.querySelectorAll('.ant-table-tbody .ant-table-row').length,
    text: document.querySelector('.ant-table-tbody')?.textContent || '',
  }))()`)
  assert('磁盘事件可切换且有数据', disk.selected && disk.rows > 0, `rows=${disk.rows}`)
  assert('累计次数已展示', disk.text.includes('204') || /\d{3,}/.test(disk.text))

  const screenshot = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false })
  fs.writeFileSync(SCREENSHOT, Buffer.from(screenshot.data, 'base64'))
  assert('页面截图已生成', fs.existsSync(SCREENSHOT), SCREENSHOT)
  assert('浏览器无脚本错误', browserErrors.length === 0, browserErrors.join(' | ').slice(0, 500))
  console.log('EVENT_PAGE_TEST_PASS')
} finally {
  try { socket?.close() } catch { /* ignore */ }
  try { chrome.kill() } catch { /* ignore */ }
}
