// End-to-end WebView input/stability smoke test.
// Starts a public WebView application, connects with input permission, sends
// real CDP mouse/keyboard events through the Web client and keeps the instance
// alive long enough to catch the historical ~20 second libcef crash.
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0'

const CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
const CMS = process.env.PX_CMS_TEST_BASE_URL || 'https://127.0.0.1:30500'
const APP_NAME = process.env.PX_WEBVIEW_TEST_APP || 'baidu'
const OBSERVE_MS = Number(process.env.PX_WEBVIEW_TEST_OBSERVE_MS || 120_000)
const INPUT_PHASE = process.env.PX_WEBVIEW_TEST_INPUT_PHASE || 'full'
const PHASE_WAIT_MS = Number(process.env.PX_WEBVIEW_TEST_PHASE_WAIT_MS || 10_000)
const ENTER_WAIT_MS = Number(process.env.PX_WEBVIEW_TEST_ENTER_WAIT_MS || PHASE_WAIT_MS)
const PRIMARY_X = Number(process.env.PX_WEBVIEW_TEST_PRIMARY_X || 0.5)
const PRIMARY_Y = Number(process.env.PX_WEBVIEW_TEST_PRIMARY_Y || 0.30)
const CDP_PORT = Number(process.env.PX_WEBVIEW_TEST_CDP_PORT || 9521)
const SCREENSHOT_PATH = process.env.PX_WEBVIEW_TEST_SCREENSHOT_PATH || ''
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

const profile = path.join(os.tmpdir(), `px-webview-e2e-${Date.now()}`)
const chrome = spawn(CHROME, [
  '--headless=new',
  '--ignore-certificate-errors',
  `--remote-debugging-port=${CDP_PORT}`,
  `--user-data-dir=${profile}`,
  '--no-first-run',
  'about:blank',
], { stdio: 'ignore' })

let ws
let id = 0
let launchedInstanceId = ''
let launchedCsrf = ''
const pending = new Map()
const command = (method, params = {}) => new Promise((resolve, reject) => {
  const requestId = ++id
  pending.set(requestId, { resolve, reject })
  ws.send(JSON.stringify({ id: requestId, method, params }))
})
const evaluate = async (expression) => {
  const response = await command('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  })
  if (response.exceptionDetails) {
    throw new Error(response.exceptionDetails.text || 'browser evaluation failed')
  }
  return response.result?.value
}

async function waitForCdp() {
  for (let retry = 0; retry < 80; retry += 1) {
    try {
      if ((await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).ok) return
    } catch { /* retry */ }
    await sleep(250)
  }
  throw new Error('Chrome DevTools endpoint did not start')
}

async function sendClick(x, y) {
  await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y })
  await command('Input.dispatchMouseEvent', {
    type: 'mousePressed', x, y, button: 'left', clickCount: 1,
  })
  await command('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x, y, button: 'left', clickCount: 1,
  })
}

async function sendText(text) {
  for (const value of text) {
    await command('Input.dispatchKeyEvent', { type: 'keyDown', text: value, key: value })
    await command('Input.dispatchKeyEvent', { type: 'keyUp', key: value })
  }
}

async function requireStream(label, waitMs = PHASE_WAIT_MS) {
  await sleep(waitMs)
  const state = await evaluate(`(() => {
    const element = document.querySelector('video')
    return {
      readyState: element?.readyState || 0,
      frames: element?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
      connection: window.__conn?.status?.() || '',
    }
  })()`).catch(() => null)
  if (!state || state.readyState < 2 || state.connection === 'closed' || state.connection === 'failed') {
    throw new Error(`${label} lost WebView connection: ${JSON.stringify(state)}`)
  }
  console.log(`PHASE ${label} stable ${JSON.stringify(state)}`)
}

async function captureRemoteFrame(filePath) {
  if (!filePath) return
  const dataUrl = await evaluate(`(() => {
    const video = document.querySelector('video')
    if (!video || video.readyState < 2) return ''
    const canvas = document.createElement('canvas')
    canvas.width = video.videoWidth
    canvas.height = video.videoHeight
    canvas.getContext('2d').drawImage(video, 0, 0)
    return canvas.toDataURL('image/png')
  })()`)
  if (!dataUrl) throw new Error('could not capture remote video frame')
  fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'))
  console.log(`SCREENSHOT ${filePath}`)
}

async function main() {
  await waitForCdp()
  const target = await (await fetch(
    `http://127.0.0.1:${CDP_PORT}/json/new?${encodeURIComponent(CMS + '/')}`,
    { method: 'PUT' },
  )).json()
  ws = new WebSocket(target.webSocketDebuggerUrl)
  ws.onmessage = ({ data }) => {
    const message = JSON.parse(data)
    if (!message.id || !pending.has(message.id)) return
    const waiter = pending.get(message.id)
    pending.delete(message.id)
    message.error ? waiter.reject(new Error(message.error.message)) : waiter.resolve(message.result)
  }
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  await command('Runtime.enable')
  await command('Page.enable')
  await command('Storage.getCookies')

  let originReady = false
  for (let retry = 0; retry < 60; retry += 1) {
    originReady = await evaluate(`location.origin === ${JSON.stringify(new URL(CMS).origin)}`)
      .catch(() => false)
    if (originReady) break
    await sleep(250)
  }
  if (!originReady) throw new Error('CMS origin did not load')

  const launch = await evaluate(`(async () => {
    const nonce = crypto.randomUUID()
    const guest = await fetch('/api/v1/session/guest', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ client_nonce: nonce }),
    }).then((response) => response.json())
    const csrf = guest.data?.csrf_token
    if (!csrf) return { error: 'guest-session-failed', guest }
    const catalog = await fetch('/api/v1/public/apps', { credentials: 'same-origin' })
      .then((response) => response.json())
    const app = (catalog.data || []).find((item) => item.name === ${JSON.stringify(APP_NAME)})
    if (!app) return { error: 'app-not-found', names: (catalog.data || []).map((item) => item.name) }
    const start = await fetch('/api/v1/public/apps/' + encodeURIComponent(app.app_id) + '/start', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
      body: JSON.stringify({ client_nonce: nonce }),
    }).then((response) => response.json())
    const instanceId = start.data?.instance_id
    if (!instanceId) return { error: 'start-failed', start }
    let instance = start.data
    for (let retry = 0; retry < 50; retry += 1) {
      const list = await fetch('/api/v1/public/instances', { credentials: 'same-origin' })
        .then((response) => response.json())
      instance = (list.data || []).find((item) => item.instance_id === instanceId) || instance
      if (instance.state === 'running' || instance.state === 'failed' || instance.state === 'stopped') break
      await new Promise((resolve) => setTimeout(resolve, 400))
    }
    if (instance.state !== 'running') return { error: 'instance-not-running', instance }
    const ticket = await fetch('/api/v1/public/instances/' + encodeURIComponent(instanceId) + '/ticket', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
      body: JSON.stringify({ client_nonce: nonce, requested_permissions: ['view', 'input'] }),
    }).then((response) => response.json())
    if (!ticket.data?.launch_url) return { error: 'ticket-failed', ticket }
    const url = new URL(ticket.data.launch_url, location.href)
    const fragment = new URLSearchParams(url.hash.replace(/^#/, ''))
    fragment.set('renew_url', location.origin + '/api/v1/connection-tickets/renew')
    if (ticket.data.renewal_token) fragment.set('renew', ticket.data.renewal_token)
    fragment.set('perms', 'view,input')
    url.hash = fragment.toString()
    return { url: url.toString(), instanceId, csrf, appId: app.app_id }
  })()`)
  if (launch.error) throw new Error(`launch failed: ${JSON.stringify(launch)}`)
  launchedInstanceId = launch.instanceId
  launchedCsrf = launch.csrf
  console.log(`START instance=${launch.instanceId} app=${launch.appId}`)

  await command('Page.navigate', { url: launch.url })
  let video = null
  for (let retry = 0; retry < 120; retry += 1) {
    video = await evaluate(`(() => {
      const element = document.querySelector('video')
      if (!element) return null
      const rect = element.getBoundingClientRect()
      return {
        readyState: element.readyState,
        width: element.videoWidth,
        height: element.videoHeight,
        x: rect.left,
        y: rect.top,
        displayWidth: rect.width,
        displayHeight: rect.height,
        connection: window.__conn?.status?.() || '',
      }
    })()`).catch(() => null)
    if (video?.readyState >= 2 && video.width > 0 && video.height > 0) break
    await sleep(500)
  }
  if (!video || video.readyState < 2) throw new Error(`video did not start: ${JSON.stringify(video)}`)
  console.log(`CONNECTED ${video.width}x${video.height} status=${video.connection}`)

  let inputReady = false
  for (let retry = 0; retry < 60; retry += 1) {
    inputReady = await evaluate(`Boolean(window.__input?.attached?.())`).catch(() => false)
    if (inputReady) break
    await sleep(250)
  }
  if (!inputReady) throw new Error('Web client input channel did not attach')
  console.log('INPUT attached')

  // Baidu's search box is near the upper-center of the remote page. Input is
  // sent through the real Web client listeners, not injected into the CEF DOM.
  const contentX = video.x + video.displayWidth * PRIMARY_X
  const contentY = video.y + video.displayHeight * PRIMARY_Y
  await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: contentX, y: contentY })
  await requireStream('mouse-move')
  if (INPUT_PHASE === 'move') return

  await sendClick(contentX, contentY)
  const firstMouse = await evaluate(`window.__input?.lastMouse?.() || null`)
  if (!firstMouse?.released) throw new Error(`mouse event was not forwarded: ${JSON.stringify(firstMouse)}`)
  const focusedInputSink = await evaluate(`(() => {
    const active = document.activeElement
    return active?.tagName === 'TEXTAREA' && active?.getAttribute('aria-hidden') === 'true'
  })()`)
  if (!focusedInputSink) throw new Error('Web client text input sink did not retain focus after video click')
  console.log(`INPUT mouse ${JSON.stringify(firstMouse)}`)
  console.log('INPUT text sink focused')
  await requireStream('mouse-click')
  if (INPUT_PHASE === 'click') {
    await captureRemoteFrame(SCREENSHOT_PATH)
    return
  }

  await evaluate(`(() => {
    window.__pxTestInputValues = []
    document.activeElement?.addEventListener('input', (event) => {
      window.__pxTestInputValues.push(event.target?.value || '')
    })
  })()`)
  await sendText('OpenAI')
  const localInputValues = await evaluate(`window.__pxTestInputValues || []`)
  if (!localInputValues.length) throw new Error('Chrome did not emit text input events into the Web client sink')
  console.log(`INPUT local text events ${JSON.stringify(localInputValues)}`)
  await requireStream('text')
  await captureRemoteFrame(SCREENSHOT_PATH)
  if (INPUT_PHASE === 'text') return

  await command('Input.dispatchKeyEvent', {
    type: 'keyDown', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40,
  })
  await command('Input.dispatchKeyEvent', {
    type: 'keyUp', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40,
  })
  await requireStream('physical-key')
  if (INPUT_PHASE === 'key') return

  await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 })
  await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 })
  await requireStream('enter', ENTER_WAIT_MS)
  if (INPUT_PHASE === 'enter') return

  // Click the first result. It normally uses target=_blank and therefore
  // exercises OnBeforePopup, the historical crash site.
  await sendClick(video.x + video.displayWidth * 0.34, video.y + video.displayHeight * 0.20)
  const startedAt = Date.now()
  while (Date.now() - startedAt < OBSERVE_MS) {
    await sleep(5_000)
    const state = await evaluate(`(() => {
      const element = document.querySelector('video')
      return {
        readyState: element?.readyState || 0,
        frames: element?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
        connection: window.__conn?.status?.() || '',
      }
    })()`).catch(() => null)
    if (!state || state.readyState < 2 || state.connection === 'closed' || state.connection === 'failed') {
      throw new Error(`WebView connection lost: ${JSON.stringify(state)}`)
    }
  }
  const finalState = await evaluate(`(() => {
    const element = document.querySelector('video')
    return {
      readyState: element?.readyState || 0,
      frames: element?.getVideoPlaybackQuality?.().totalVideoFrames || 0,
      connection: window.__conn?.status?.() || '',
    }
  })()`)
  console.log(`PASS WebView input and ${OBSERVE_MS}ms stability ${JSON.stringify(finalState)}`)
}

main()
  .catch((error) => { console.error(`FAIL ${error.message}`); process.exitCode = 1 })
  .finally(async () => {
    if (ws?.readyState === WebSocket.OPEN && launchedInstanceId && launchedCsrf) {
      try {
        await command('Page.navigate', { url: CMS + '/' })
        for (let retry = 0; retry < 20; retry += 1) {
          if (await evaluate(`location.origin === ${JSON.stringify(new URL(CMS).origin)}`).catch(() => false)) break
          await sleep(100)
        }
        const stopped = await evaluate(`fetch(
          '/api/v1/public/instances/' + ${JSON.stringify(launchedInstanceId)} + '/stop', {
            method: 'POST', credentials: 'same-origin',
            headers: {
              'Content-Type': 'application/json',
              'X-CSRF-Token': ${JSON.stringify(launchedCsrf)},
            },
            body: JSON.stringify({ reason: 'automated WebView stability test complete' }),
          }
        ).then(async (response) => ({
          ok: response.ok,
          status: response.status,
          body: await response.text(),
        }))`)
        console.log(`CLEANUP instance=${launchedInstanceId} ${JSON.stringify(stopped)}`)
      } catch { /* the instance may already have crashed and been reaped */ }
    }
    try { ws?.close() } catch { /* ignore */ }
    try { chrome.kill() } catch { /* ignore */ }
  })
