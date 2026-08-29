// Production E2E for the Console record tunnel and records management page.
// Usage: set PX_CONSOLE_TEST_PASSWORD, then run node scripts/cdp_records_e2e.mjs
import { spawn } from 'node:child_process'
import os from 'node:os'
import path from 'node:path'
import fs from 'node:fs'

const CHROME = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const BASE = process.env.CONSOLE_URL || process.env.CMS_URL || 'https://10.0.0.16:30500'
const DEVICE = process.env.DEVICE_ID || '001190520'
const USERNAME = process.env.PX_CONSOLE_TEST_USERNAME || 'ConsoleAdmin'
const PASSWORD = process.env.PX_CONSOLE_TEST_PASSWORD || process.env.PX_CMS_TEST_PASSWORD || ''
const REQUESTED_FILE = process.env.PLAY_FILE || ''
const WAIT_VIDEO_MS = Number(process.env.WAIT_VIDEO_MS || 120000)
const OUT = process.env.OUT || path.join(os.tmpdir(), `records_e2e_${Date.now()}.png`)
const CDP_PORT = Number(process.env.CDP_PORT || 9491)
const FORCE_CONSOLE_TUNNEL = process.env.FORCE_CONSOLE_TUNNEL !== '0'

if (!PASSWORD) throw new Error('PX_CONSOLE_TEST_PASSWORD is required')

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
const profile = path.join(os.tmpdir(), `cdp-rec-${Date.now()}`)
const chrome = spawn(CHROME, [
  '--headless=new',
  `--remote-debugging-port=${CDP_PORT}`,
  `--user-data-dir=${profile}`,
  '--no-first-run',
  '--ignore-certificate-errors',
  '--window-size=1600,900',
  'about:blank',
], { stdio: 'ignore' })

const stopChrome = () => {
  try { chrome.kill() } catch { /* already stopped */ }
}
process.on('exit', stopChrome)
process.on('SIGINT', () => process.exit(130))

let msgId = 0
const pending = new Map()
let ws
const consoleErrors = []
const netRequests = new Map()
const netDone = []

function cmd(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++msgId
    pending.set(id, { resolve, reject })
    ws.send(JSON.stringify({ id, method, params }))
  })
}

async function evaluate(expression) {
  const result = await cmd('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  })
  if (result.exceptionDetails) {
    throw new Error(JSON.stringify(result.exceptionDetails).slice(0, 1000))
  }
  return result.result?.value
}

async function waitDevtools() {
  for (let attempt = 0; attempt < 60; attempt++) {
    try {
      const response = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)
      if (response.ok) return
    } catch { /* retry */ }
    await sleep(500)
  }
  throw new Error('devtools not ready')
}

async function waitFor(expression, timeoutMs = 15000, label = expression) {
  const started = Date.now()
  while (Date.now() - started < timeoutMs) {
    try {
      if (await evaluate(expression)) return
    } catch { /* page may be navigating */ }
    await sleep(500)
  }
  throw new Error(`timeout waiting: ${label}`)
}

async function navigate(url) {
  await cmd('Page.navigate', { url })
  await waitFor('document.readyState === "complete"', 30000, `page load ${url}`)
}

async function login() {
  await navigate(`${BASE}/`)
  await waitFor(
    `[...document.querySelectorAll('input')].some((element) => element.placeholder === '请输入密码')`,
    30000,
    'admin login form',
  )
  const controls = await evaluate(`(() => {
    const inputs = [...document.querySelectorAll('input')]
    const username = inputs.find((element) => element.placeholder === '请输入')
    const password = inputs.find((element) => element.placeholder === '请输入密码')
    const button = [...document.querySelectorAll('button')]
      .find((element) => element.textContent.replace(/\\s/g, '') === '登录')
    if (!username || !password || !button) return false
    const setValue = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set
    setValue.call(username, ${JSON.stringify(USERNAME)})
    username.dispatchEvent(new Event('input', { bubbles: true }))
    setValue.call(password, ${JSON.stringify(PASSWORD)})
    password.dispatchEvent(new Event('input', { bubbles: true }))
    button.click()
    return true
  })()`)
  if (!controls) throw new Error('admin login controls are incomplete')
  await waitFor(`location.pathname !== '/'`, 20000, 'admin navigation after login')
  const session = await evaluate(`fetch('/api/v1/session/admin/me', {
    credentials: 'same-origin',
  }).then(async (response) => ({ status: response.status, body: await response.json() }))`)
  if (session?.status !== 200 || session?.body?.code !== 200) {
    throw new Error(`admin session failed: ${JSON.stringify(session)}`)
  }
  console.log('ADMIN_SESSION: OK')
}

async function recordList() {
  return evaluate(`fetch('/api/v1/record/list?device_id=${encodeURIComponent(DEVICE)}', {
    credentials: 'same-origin',
  }).then(async (response) => ({ status: response.status, body: await response.json() }))`)
}

async function removeConsoleCopy(id) {
  return evaluate(`fetch('/api/v1/record/' + encodeURIComponent(${JSON.stringify(id)}), {
    method: 'DELETE',
    credentials: 'same-origin',
    headers: { 'X-CSRF-Token': sessionStorage.getItem('px_admin_csrf') || '' },
  }).then(async (response) => ({ status: response.status, body: await response.json() }))`)
}

await waitDevtools()
const targetResponse = await fetch(
  `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(BASE + '/')}`,
  { method: 'PUT' },
)
const target = await targetResponse.json()
ws = new WebSocket(target.webSocketDebuggerUrl)
ws.onmessage = (event) => {
  const message = JSON.parse(event.data)
  if (message.id && pending.has(message.id)) {
    const operation = pending.get(message.id)
    pending.delete(message.id)
    message.error ? operation.reject(new Error(message.error.message)) : operation.resolve(message.result)
    return
  }
  if (message.method === 'Runtime.exceptionThrown') {
    consoleErrors.push(`EXC: ${JSON.stringify(message.params.exceptionDetails).slice(0, 400)}`)
  }
  if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
    consoleErrors.push(`CONSOLE: ${message.params.args.map((arg) => arg.value ?? arg.description ?? '').join(' ').slice(0, 400)}`)
  }
  if (message.method === 'Network.requestWillBeSent') {
    netRequests.set(message.params.requestId, message.params.request.url)
  }
  if (message.method === 'Network.responseReceived') {
    const url = netRequests.get(message.params.requestId)
    if (url) netDone.push({ url, status: message.params.response.status })
  }
}
await new Promise((resolve, reject) => {
  ws.onopen = resolve
  ws.onerror = reject
})
await cmd('Runtime.enable')
await cmd('Network.enable')
if (FORCE_CONSOLE_TUNNEL) {
  // The management page prefers a LAN-direct Panel endpoint when it is
  // reachable. Block only that endpoint in this browser so the production
  // Console tunnel (RecordFetchReq -> multipart upload -> playback) is tested
  // deterministically, even when the E2E runner shares the Panel's LAN.
  await cmd('Network.setBlockedURLs', { urls: ['http://*:20369/*'] })
}
await cmd('Page.enable')

await login()

// Force a production tunnel list request before opening the page. Choose one
// playable device-side file and remove an old Console cache entry so this run
// must execute RecordFetchReq -> multipart upload -> RecordFetchDone.
let listResponse = await recordList()
if (listResponse?.status !== 200 || listResponse?.body?.code !== 200) {
  throw new Error(`record list failed: ${JSON.stringify(listResponse)}`)
}
let items = listResponse.body.data?.files || []
let targetFile = REQUESTED_FILE
  ? items.find((item) => item.name.includes(REQUESTED_FILE))
  : items.find((item) => !item.codec || item.codec === 'h264')
if (!targetFile) throw new Error(`no playable record returned for device ${DEVICE}`)
if (targetFile.id) {
  const deleted = await removeConsoleCopy(targetFile.id)
  if (deleted?.status !== 200 || deleted?.body?.code !== 200) {
    throw new Error(`precondition delete failed: ${JSON.stringify(deleted)}`)
  }
  listResponse = await recordList()
  items = listResponse.body.data?.files || []
  targetFile = items.find((item) => item.name === targetFile.name)
}
if (!targetFile || targetFile.state === 'ready') {
  throw new Error('failed to establish a fresh record-fetch precondition')
}
const targetName = targetFile.name
console.log(`TUNNEL_LIST: OK files=${items.length} target=${targetName}`)

await navigate(`${BASE}/records/${DEVICE}`)
await waitFor(
  `[...document.querySelectorAll('.ant-table-row')].some((row) => row.innerText.includes(${JSON.stringify(targetName)}))`,
  30000,
  `record row ${targetName}`,
)
const header = await evaluate(`(document.querySelector('.ant-card-head')?.innerText || '').replace(/\\n/g, ' | ')`)
console.log(`HEADER: ${header}`)

const playClicked = await evaluate(`(() => {
  const row = [...document.querySelectorAll('.ant-table-row')]
    .find((element) => element.innerText.includes(${JSON.stringify(targetName)}))
  const button = row && [...row.querySelectorAll('button')]
    .find((element) => /播\\s*放/.test(element.textContent))
  if (!button || button.disabled) return false
  button.click()
  return true
})()`)
if (!playClicked) throw new Error('play button was not actionable')

await waitFor(`!!document.querySelector('video')`, WAIT_VIDEO_MS, 'record video element')
await waitFor(`document.querySelector('video').readyState >= 2`, 30000, 'record video ready state')
const video = await evaluate(`({
  src: document.querySelector('video').src,
  duration: document.querySelector('video').duration,
})`)
if (!Number.isFinite(video.duration) || video.duration <= 0) {
  throw new Error(`invalid record duration: ${JSON.stringify(video)}`)
}
const seekTo = Math.max(1, Math.floor(video.duration * 0.6))
await evaluate(`new Promise((resolve, reject) => {
  const video = document.querySelector('video')
  const timeout = setTimeout(() => reject(new Error('seek timeout')), 10000)
  video.addEventListener('seeked', () => {
    clearTimeout(timeout)
    resolve(video.currentTime)
  }, { once: true })
  video.currentTime = ${seekTo}
})`)
await sleep(1000)
const playbackUrl = new URL(video.src)
console.log(`PLAYBACK: OK duration=${video.duration} seek=${seekTo} source=${playbackUrl.origin}${playbackUrl.pathname}`)

// Close the player, retain the fetched copy on Console, then remove it through
// the real confirmation UI. The device-side recording must remain listed.
const playerClosed = await evaluate(`(() => {
  const video = document.querySelector('video')
  const button = video?.closest('.ant-modal')?.querySelector('.ant-modal-close')
  if (!button) return false
  button.click()
  return true
})()`)
if (!playerClosed) throw new Error('player close button was not actionable')
await waitFor(`!document.querySelector('video')`, 10000, 'player close')
await waitFor(
  `[...document.querySelectorAll('.ant-table-row')].some((row) =>
    row.innerText.includes(${JSON.stringify(targetName)}) && row.innerText.includes('已回传'))`,
  20000,
  'fetched record state',
)
const keepClicked = await evaluate(`(() => {
  const row = [...document.querySelectorAll('.ant-table-row')]
    .find((element) => element.innerText.includes(${JSON.stringify(targetName)}))
  const button = row && [...row.querySelectorAll('button')]
    .find((element) => element.textContent.replace(/\\s/g, '') === '下载到Console')
  if (!button || button.disabled) return false
  button.click()
  return true
})()`)
if (!keepClicked) throw new Error('download-to-Console button was not actionable')
await waitFor(
  `[...document.querySelectorAll('.ant-table-row')].some((row) =>
    row.innerText.includes(${JSON.stringify(targetName)}) && row.innerText.includes('已存 Console'))`,
  20000,
  'kept Console record state',
)

const screenshot = await cmd('Page.captureScreenshot', { format: 'png' })
fs.writeFileSync(OUT, Buffer.from(screenshot.data, 'base64'))

const deleteClicked = await evaluate(`(() => {
  const row = [...document.querySelectorAll('.ant-table-row')]
    .find((element) => element.innerText.includes(${JSON.stringify(targetName)}))
  const button = row && [...row.querySelectorAll('button')]
    .find((element) => element.textContent.replace(/\\s/g, '') === '删除')
  if (!button || button.disabled) return false
  button.click()
  return true
})()`)
if (!deleteClicked) throw new Error('delete button was not actionable')
await waitFor(
  `[...document.querySelectorAll('.ant-popover button')].some((button) =>
    button.textContent.replace(/\\s/g, '') === '删除')`,
  10000,
  'delete confirmation',
)
await evaluate(`(() => {
  const button = [...document.querySelectorAll('.ant-popover button')]
    .find((element) => element.textContent.replace(/\\s/g, '') === '删除')
  button.click()
})()`)
await waitFor(
  `[...document.querySelectorAll('.ant-table-row')].some((row) =>
    row.innerText.includes(${JSON.stringify(targetName)}) && !row.innerText.includes('已存 Console'))`,
  30000,
  'Console copy deletion',
)

const finalList = await recordList()
const finalItem = finalList?.body?.data?.files?.find((item) => item.name === targetName)
if (finalList?.status !== 200 || finalList?.body?.code !== 200 || !finalItem || finalItem.keep) {
  throw new Error(`final record state invalid: ${JSON.stringify(finalList)}`)
}

const relevantResponses = netDone.filter(({ url }) =>
  url.includes('/api/v1/record/') || url.includes('/uploads/records/'))
const failedResponse = relevantResponses.find(({ status }) => status >= 400)
const hasMediaRange = relevantResponses.some(({ url, status }) =>
  url.includes('/uploads/records/') && (status === 200 || status === 206))
if (failedResponse || !hasMediaRange) {
  throw new Error(`record network audit failed: ${JSON.stringify(relevantResponses)}`)
}
if (consoleErrors.length) {
  throw new Error(`browser console errors: ${JSON.stringify(consoleErrors.slice(0, 10))}`)
}

console.log(`NETWORK: ${JSON.stringify(relevantResponses)}`)
console.log(`SCREENSHOT: ${OUT}`)
console.log('RECORD_E2E_PASS')
ws.close()
stopChrome()
